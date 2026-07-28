#include "wifi_manager.h"
#include "config_store.h"
#include "pico/cyw43_arch.h"
#include "pico/util/queue.h"
#include "FreeRTOS.h"
#include "task.h"
#include "lwip/tcp.h"
#include "lwip/apps/mdns.h"
#include "ws_crypto.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_NETWORKS 16
typedef struct {char ssid[33];int16_t rssi;uint8_t auth;} network_t;
static device_config_t cfg;
static volatile wifi_state_t state;
static volatile bool connect_requested,scan_requested,scan_running;
static volatile uint32_t provisioning_until_ms;
static char ip[20],telemetry[448]="{\"state\":\"BOOT\"}",config_json[2][1024]={"{}",""},scan_json[2][900]={"{\"scanning\":false,\"networks\":[]}",""};static volatile uint8_t config_json_active,scan_json_active;
static network_t networks[MAX_NETWORKS];static volatile uint8_t network_count;
static queue_t commands,configurations;static struct tcp_pcb *ws_client;static uint8_t ws_rx[1024];static size_t ws_rx_len;
static bool mdns_initialized,mdns_registered;
static uint32_t connect_started_ms;
static uint32_t autoconnect_due_ms;

static const char page[]=
"<!doctype html><html lang=fr><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
"<title>Configuration PasteDispenser</title><style>:root{color-scheme:dark;--bg:#07131f;--panel:#102335;--line:#29445b;--text:#edf7ff;--muted:#9bb2c4;--blue:#258bd2;--green:#43d17c}*{box-sizing:border-box}body{margin:0;background:radial-gradient(circle at top,#173954,var(--bg) 55%);color:var(--text);font:16px system-ui,sans-serif}main{width:min(920px,100%);margin:auto;padding:24px}h1{margin-bottom:4px}.muted,label{color:var(--muted)}section{background:linear-gradient(145deg,#142b40,var(--panel));border:1px solid var(--line);border-radius:18px;padding:20px;margin:20px 0;box-shadow:0 12px 30px #0005}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:14px}label{display:block}input{display:block;width:100%;margin-top:6px;padding:11px;border:1px solid var(--line);border-radius:9px;background:#071623;color:white;font:inherit}button{padding:13px 20px;margin:5px;border:0;border-radius:10px;background:var(--blue);color:white;font-weight:700;cursor:pointer}button:active{transform:translateY(2px)}#msg{min-height:24px;color:var(--green)}</style>"
"<main><h1>PasteDispenser</h1><div class=muted>Configuration persistante des paramètres par défaut</div><section><h2>Paramètres</h2><form id=form><div id=fields class=grid></div><p><button>Enregistrer</button><button type=button id=reload>Relire</button><button type=button id=reboot>Redémarrer la seringue</button></p><div id=msg></div></form></section><section><h2>État</h2><pre id=status>Chargement…</pre></section></main>"
"<script>const defs={screw_pitch_mm:'Pas de vis (mm/tr)',motor_steps_per_rev:'Pas moteur par tour',microsteps:'Micro-pas',motor_run_current_mA:'Courant moteur (mA)',motor_hold_current_mA:'Courant maintien (mA)',manual_speed_mm_s:'Vitesse manuelle (mm/s)',dosing_speed_mm_s:'Vitesse de fourniture (mm/s)',trigger_dose_mm:'Quantité par défaut (mm)',retract_distance_mm:'Recul (mm)',retract_speed_mm_s:'Vitesse recul (mm/s)',retract_delay_ms:'Délai recul (ms)',position_min_mm:'Position minimale (mm)',position_max_mm:'Position maximale (mm)',manual_timeout_ms:'Timeout manuel (ms)',stallguard_threshold:'Seuil StallGuard',stallguard_warning_level:'Avertissement StallGuard',stallguard_critical_level:'Critique StallGuard',stallguard_filter_count:'Filtrage StallGuard',stallguard_enabled:'StallGuard actif (0/1)'};let current={};const $=id=>document.getElementById(id);$('fields').innerHTML=Object.entries(defs).map(([k,v])=>`<label>${v}<input name=${k} type=number step=any required></label>`).join('');async function load(){current=await fetch('/api/config').then(r=>r.json());for(const[k]of Object.entries(defs))$('form').elements[k].value=current[k];$('status').textContent=JSON.stringify(await fetch('/api/status').then(r=>r.json()),null,2);$('msg').textContent='Configuration chargée'}async function command(o){const r=await fetch('/api/command',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(o)});return(await r.json()).ok}$('form').onsubmit=async e=>{e.preventDefault();$('msg').textContent='Validation et enregistrement…';const values={...current};for(const[k]of Object.entries(defs))values[k]=+e.target.elements[k].value;const r=await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(values)}).then(r=>r.json());if(!r.ok){$('msg').textContent='Configuration refusée : vérifier les valeurs';return}$('msg').textContent='Configuration enregistrée. Redémarrer pour appliquer les réglages mécaniques.';setTimeout(load,500)};$('reload').onclick=load;$('reboot').onclick=()=>command({command:'reboot'});load().catch(e=>$('msg').textContent='Erreur: '+e.message);</script>"
"</html>";


static void enqueue_stop(void){machine_command_t c={.kind=CMD_STOP},drop;if(!queue_try_add(&commands,&c)){queue_try_remove(&commands,&drop);queue_try_add(&commands,&c);}}
static void ws_closed(void){if(ws_client){ws_client=NULL;enqueue_stop();}}
static void tcp_error(void *arg,err_t err){(void)arg;(void)err;ws_closed();}
static err_t http_sent(void*a,struct tcp_pcb*p,u16_t len){(void)a;(void)len;tcp_close(p);return ERR_OK;}
static void http_reply(struct tcp_pcb*p,const char*type,const char*body){char h[160];int n=snprintf(h,sizeof(h),"HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %u\r\nConnection: close\r\n\r\n",type,(unsigned)strlen(body));tcp_write(p,h,n,TCP_WRITE_FLAG_COPY);tcp_write(p,body,strlen(body),TCP_WRITE_FLAG_COPY);tcp_sent(p,http_sent);tcp_output(p);}

static bool json_value(const char *json,const char *name,float *value){char key[48];snprintf(key,sizeof(key),"\"%s\"",name);const char *p=strstr(json,key);if(!p||(p=strchr(p,':'))==NULL)return false;char *end;*value=strtof(p+1,&end);return end!=p+1;}
static bool parse_config_request(const char *json,device_config_t *out){float v;device_config_t c=cfg;
#define READ_FLOAT(name) do{if(!json_value(json,#name,&v)||!isfinite(v))return false;c.name=v;}while(0)
#define READ_U16(name) do{if(!json_value(json,#name,&v)||!isfinite(v)||v<0||v>65535||truncf(v)!=v)return false;c.name=(uint16_t)v;}while(0)
#define READ_U32(name) do{if(!json_value(json,#name,&v)||!isfinite(v)||v<0||v>4294967040.0f||truncf(v)!=v)return false;c.name=(uint32_t)v;}while(0)
 READ_FLOAT(screw_pitch_mm);READ_U16(motor_steps_per_rev);READ_U16(microsteps);READ_U16(motor_run_current_mA);READ_U16(motor_hold_current_mA);
 READ_FLOAT(manual_speed_mm_s);READ_FLOAT(dosing_speed_mm_s);READ_FLOAT(trigger_dose_mm);READ_FLOAT(a1_mm_s2);READ_FLOAT(amax_mm_s2);READ_FLOAT(dmax_mm_s2);READ_FLOAT(d1_mm_s2);
 READ_FLOAT(retract_distance_mm);READ_FLOAT(retract_speed_mm_s);READ_U32(retract_delay_ms);READ_FLOAT(position_min_mm);READ_FLOAT(position_max_mm);READ_U32(manual_timeout_ms);
 if(!json_value(json,"stallguard_threshold",&v)||!isfinite(v)||v<-64||v>63||truncf(v)!=v)return false;
 c.stallguard_threshold=(int8_t)v;
 READ_U16(stallguard_warning_level);READ_U16(stallguard_critical_level);READ_U16(stallguard_filter_count);
 if(!json_value(json,"stallguard_enabled",&v)||(v!=0&&v!=1))return false;
 c.stallguard_enabled=(uint8_t)v;
#undef READ_FLOAT
#undef READ_U16
#undef READ_U32
 if(!device_config_validate(&c))return false;
 *out=c;
 return true;
}

static bool websocket_upgrade(struct tcp_pcb*p,const char*req){const char*k=strstr(req,"Sec-WebSocket-Key:");if(!k)return false;k+=18;while(*k==' ')k++;const char*end=strstr(k,"\r\n");if(!end)return false;char accept[29];if(!ws_crypto_accept(k,(size_t)(end-k),accept))return false;char response[180];int n=snprintf(response,sizeof(response),"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: %s\r\n\r\n",accept);tcp_write(p,response,n,TCP_WRITE_FLAG_COPY);tcp_output(p);if(ws_client&&ws_client!=p)tcp_abort(ws_client);ws_client=p;tcp_err(p,tcp_error);return true;}

static bool websocket_data(const uint8_t*d,size_t n){if(ws_rx_len+n>sizeof(ws_rx))return false;memcpy(ws_rx+ws_rx_len,d,n);ws_rx_len+=n;size_t consumed=0;while(ws_rx_len-consumed>=2){uint8_t*frame=ws_rx+consumed;uint8_t opcode=frame[0]&0x0f;size_t len=frame[1]&0x7f,pos=2;if(len==126){if(ws_rx_len-consumed<4)break;len=((size_t)frame[2]<<8)|frame[3];pos=4;}else if(len==127)return false;if(!(frame[1]&0x80)||len>511)return false;size_t frame_len=pos+4+len;if(ws_rx_len-consumed<frame_len)break;if(opcode==8){enqueue_stop();return false;}if(opcode==1){const uint8_t*mask=frame+pos;pos+=4;char json[512];for(size_t i=0;i<len;i++)json[i]=(char)(frame[pos+i]^mask[i&3]);json[len]=0;machine_command_t c;if(command_parse_json(json,len,&c)){if(c.kind==CMD_STOP)enqueue_stop();else queue_try_add(&commands,&c);}}consumed+=frame_len;}if(consumed){memmove(ws_rx,ws_rx+consumed,ws_rx_len-consumed);ws_rx_len-=consumed;}return true;}

static err_t recv_cb(void*a,struct tcp_pcb*p,struct pbuf*b,err_t e){(void)a;if(!b){if(p==ws_client)ws_closed();tcp_close(p);return ERR_OK;}uint8_t reqbuf[1024];size_t n=b->tot_len<sizeof(reqbuf)-1?b->tot_len:sizeof(reqbuf)-1;pbuf_copy_partial(b,reqbuf,n,0);reqbuf[n]=0;tcp_recved(p,b->tot_len);pbuf_free(b);if(e!=ERR_OK){if(p==ws_client)ws_closed();tcp_abort(p);return ERR_ABRT;}if(p==ws_client){if(!websocket_data(reqbuf,n)){ws_closed();tcp_close(p);}return ERR_OK;}const char*req=(const char*)reqbuf;if(!strncmp(req,"GET /ws ",8)&&strstr(req,"Upgrade: websocket")){ws_rx_len=0;if(!websocket_upgrade(p,req))http_reply(p,"text/plain","Bad WebSocket request");}else if(!strncmp(req,"GET /api/status",15))http_reply(p,"application/json",telemetry);else if(!strncmp(req,"GET /api/config",15))http_reply(p,"application/json",config_json[config_json_active]);else if(!strncmp(req,"POST /api/config",16)){char*body=strstr(req,"\r\n\r\n");device_config_t requested;if(body&&parse_config_request(body+4,&requested)&&queue_try_add(&configurations,&requested))http_reply(p,"application/json","{\"ok\":true,\"queued\":true}");else http_reply(p,"application/json","{\"ok\":false,\"queued\":false}");}else if(!strncmp(req,"POST /api/command",17)){char*body=strstr(req,"\r\n\r\n");machine_command_t c;if(body&&command_parse_json(body+4,strlen(body+4),&c)&&queue_try_add(&commands,&c))http_reply(p,"application/json","{\"ok\":true,\"queued\":true}");else http_reply(p,"application/json","{\"ok\":false,\"queued\":false}");}else http_reply(p,"text/html; charset=utf-8",page);return ERR_OK;}
static err_t accept_cb(void*a,struct tcp_pcb*n,err_t e){(void)a;if(e!=ERR_OK)return e;tcp_recv(n,recv_cb);return ERR_OK;}
static void server_start(void){cyw43_arch_lwip_begin();struct tcp_pcb*p=tcp_new_ip_type(IPADDR_TYPE_ANY);if(p&&tcp_bind(p,NULL,80)==ERR_OK){p=tcp_listen_with_backlog(p,2);tcp_accept(p,accept_cb);}else if(p)tcp_close(p);cyw43_arch_lwip_end();}
static void mdns_http_txt(struct mdns_service*service,void*arg){(void)arg;mdns_resp_add_service_txtitem(service,"path=/",6);}
static void mdns_start(void){cyw43_arch_lwip_begin();if(!mdns_initialized){mdns_resp_init();mdns_initialized=true;}if(mdns_registered)mdns_resp_remove_netif(netif_default);netif_set_hostname(netif_default,"dispenser");if(mdns_resp_add_netif(netif_default,"dispenser")==ERR_OK){mdns_registered=true;mdns_resp_add_service(netif_default,"PasteDispenser","_http",DNSSD_PROTO_TCP,80,mdns_http_txt,NULL);}else mdns_registered=false;cyw43_arch_lwip_end();}

static int scan_result(void*env,const cyw43_ev_scan_result_t*r){(void)env;if(!r||!r->ssid_len)return 0;uint8_t count=network_count;for(uint8_t i=0;i<count;i++)if(strlen(networks[i].ssid)==r->ssid_len&&!memcmp(networks[i].ssid,r->ssid,r->ssid_len)){if(r->rssi>networks[i].rssi)networks[i].rssi=r->rssi;return 0;}if(count>=MAX_NETWORKS)return 0;network_t*n=&networks[count];memcpy(n->ssid,r->ssid,r->ssid_len);n->ssid[r->ssid_len]=0;n->rssi=r->rssi;n->auth=r->auth_mode;network_count=count+1;return 0;}
static void set_scan_json(const char*text){uint8_t next=scan_json_active^1u;snprintf(scan_json[next],sizeof(scan_json[next]),"%s",text);scan_json_active=next;}
static void format_scan_results(void){for(uint8_t i=0;i<network_count;i++)for(uint8_t j=i+1;j<network_count;j++)if(networks[j].rssi>networks[i].rssi){network_t t=networks[i];networks[i]=networks[j];networks[j]=t;}uint8_t next=scan_json_active^1u;char*out=scan_json[next];size_t used=snprintf(out,sizeof(scan_json[next]),"{\"scanning\":false,\"networks\":[");for(uint8_t i=0;i<network_count&&used<sizeof(scan_json[next])-64;i++){char escaped[67];size_t e=0;for(size_t j=0;networks[i].ssid[j]&&e<sizeof(escaped)-2;j++){char c=networks[i].ssid[j];if(c=='\"'||c=='\\')escaped[e++]='\\';if((unsigned char)c>=32)escaped[e++]=c;}escaped[e]=0;used+=snprintf(out+used,sizeof(scan_json[next])-used,"%s{\"ssid\":\"%s\",\"rssi\":%d,\"secure\":%s}",i?",":"",escaped,networks[i].rssi,networks[i].auth?"true":"false");}snprintf(out+used,sizeof(scan_json[next])-used,"]}");scan_json_active=next;}

static void connection_succeeded(void){state=WIFI_CONNECTED;provisioning_until_ms=0;snprintf(ip,sizeof(ip),"%s",ip4addr_ntoa(netif_ip4_addr(netif_default)));device_config_t saved;config_store_load(&saved);snprintf(saved.wifi_ssid,sizeof(saved.wifi_ssid),"%s",cfg.wifi_ssid);snprintf(saved.wifi_password,sizeof(saved.wifi_password),"%s",cfg.wifi_password);config_store_save(&saved);cfg=saved;server_start();mdns_start();}

static void network_task(void*arg){(void)arg;while(true){if(scan_requested&&!scan_running){scan_requested=false;scan_running=true;network_count=0;set_scan_json("{\"scanning\":true,\"networks\":[]}");cyw43_arch_enable_sta_mode();cyw43_wifi_scan_options_t opts={0};if(cyw43_wifi_scan(&cyw43_state,&opts,NULL,scan_result)){scan_running=false;format_scan_results();}}
 if(autoconnect_due_ms&&(int32_t)(to_ms_since_boot(get_absolute_time())-autoconnect_due_ms)>=0){autoconnect_due_ms=0;connect_requested=true;}
 if(scan_running&&!cyw43_wifi_scan_active(&cyw43_state)){scan_running=false;format_scan_results();}
 if(connect_requested&&!scan_running){connect_requested=false;state=WIFI_CONNECTING;cyw43_arch_enable_sta_mode();int rc=cyw43_arch_wifi_connect_async(cfg.wifi_ssid,cfg.wifi_password,CYW43_AUTH_WPA2_AES_PSK);if(rc){state=WIFI_AUTH_FAILED;}else connect_started_ms=to_ms_since_boot(get_absolute_time());}
 if(state==WIFI_CONNECTING){int link=cyw43_wifi_link_status(&cyw43_state,CYW43_ITF_STA);if(link==CYW43_LINK_JOIN)connection_succeeded();else if(link==CYW43_LINK_BADAUTH)state=WIFI_AUTH_FAILED;else if(link==CYW43_LINK_NONET)state=WIFI_NOT_FOUND;else if(link==CYW43_LINK_FAIL)state=WIFI_AUTH_FAILED;else if(to_ms_since_boot(get_absolute_time())-connect_started_ms>=20000)state=WIFI_TIMEOUT;}
 vTaskDelay(pdMS_TO_TICKS(25));}}

void wifi_manager_publish_config(const device_config_t*c){if(!c)return;cfg=*c;uint8_t next=config_json_active^1u;snprintf(config_json[next],sizeof(config_json[next]),"{\"screw_pitch_mm\":%.3f,\"motor_steps_per_rev\":%u,\"microsteps\":%u,\"motor_run_current_mA\":%u,\"motor_hold_current_mA\":%u,\"manual_speed_mm_s\":%.3f,\"dosing_speed_mm_s\":%.3f,\"trigger_dose_mm\":%.3f,\"a1_mm_s2\":%.3f,\"amax_mm_s2\":%.3f,\"dmax_mm_s2\":%.3f,\"d1_mm_s2\":%.3f,\"retract_distance_mm\":%.3f,\"retract_speed_mm_s\":%.3f,\"retract_delay_ms\":%lu,\"position_min_mm\":%.3f,\"position_max_mm\":%.3f,\"manual_timeout_ms\":%lu,\"stallguard_threshold\":%d,\"stallguard_warning_level\":%u,\"stallguard_critical_level\":%u,\"stallguard_filter_count\":%u,\"stallguard_enabled\":%u}",c->screw_pitch_mm,c->motor_steps_per_rev,c->microsteps,c->motor_run_current_mA,c->motor_hold_current_mA,c->manual_speed_mm_s,c->dosing_speed_mm_s,c->trigger_dose_mm,c->a1_mm_s2,c->amax_mm_s2,c->dmax_mm_s2,c->d1_mm_s2,c->retract_distance_mm,c->retract_speed_mm_s,(unsigned long)c->retract_delay_ms,c->position_min_mm,c->position_max_mm,(unsigned long)c->manual_timeout_ms,c->stallguard_threshold,c->stallguard_warning_level,c->stallguard_critical_level,c->stallguard_filter_count,c->stallguard_enabled);config_json_active=next;}

void wifi_manager_init(const device_config_t*c){cfg=*c;wifi_manager_publish_config(c);queue_init(&commands,sizeof(machine_command_t),4);queue_init(&configurations,sizeof(device_config_t),1);state=WIFI_IDLE;if(!cfg.wifi_ssid[0])wifi_manager_open_provisioning(300000);configASSERT(xTaskCreate(network_task,"wifi",2048,NULL,3,NULL)==pdPASS);
#if DISPENSER_WIFI_AUTOCONNECT
 /* Starting a Wi-Fi join while the BT controller is still entering the
    WORKING state can leave ATT discovery unanswered. Give BLE five seconds
    to finish initialization before reconnecting saved Wi-Fi credentials. */
 if(cfg.wifi_ssid[0])autoconnect_due_ms=to_ms_since_boot(get_absolute_time())+5000;
#endif
}
void wifi_manager_open_provisioning(uint32_t duration_ms){provisioning_until_ms=to_ms_since_boot(get_absolute_time())+duration_ms;}
bool wifi_manager_provisioning_open(void){return provisioning_until_ms&&((int32_t)(provisioning_until_ms-to_ms_since_boot(get_absolute_time()))>0);}
bool wifi_manager_set_ssid(const uint8_t*d,size_t n){if(n>32)return false;memcpy(cfg.wifi_ssid,d,n);cfg.wifi_ssid[n]=0;return true;}
bool wifi_manager_set_password(const uint8_t*d,size_t n){if(n>64)return false;memcpy(cfg.wifi_password,d,n);cfg.wifi_password[n]=0;return true;}
bool wifi_manager_request_connect(void){if(!cfg.wifi_ssid[0]||state==WIFI_CONNECTING)return false;connect_requested=true;return true;}
bool wifi_manager_request_scan(void){if(scan_running||scan_requested)return false;scan_requested=true;return true;}
const char*wifi_manager_scan_results(void){return scan_json[scan_json_active];}
wifi_state_t wifi_manager_state(void){return state;}
const char*wifi_manager_state_name(void){static const char*n[]={"IDLE","CONNECTING","CONNECTED","AUTH_FAILED","NETWORK_NOT_FOUND","TIMEOUT"};return n[state];}
const char*wifi_manager_ip(void){return ip;}
bool wifi_manager_take_command(machine_command_t*c){return queue_try_remove(&commands,c);}
bool wifi_manager_take_config(device_config_t*c){return queue_try_remove(&configurations,c);}
void wifi_manager_publish(const char*j){size_t n=strlen(j);if(n>=sizeof(telemetry))n=sizeof(telemetry)-1;cyw43_arch_lwip_begin();memcpy(telemetry,j,n);telemetry[n]=0;if(ws_client){uint8_t frame[452];size_t pos=0;frame[pos++]=0x81;if(n<126)frame[pos++]=(uint8_t)n;else{frame[pos++]=126;frame[pos++]=(uint8_t)(n>>8);frame[pos++]=(uint8_t)n;}memcpy(frame+pos,telemetry,n);if(tcp_write(ws_client,frame,pos+n,TCP_WRITE_FLAG_COPY)==ERR_OK)tcp_output(ws_client);}cyw43_arch_lwip_end();}

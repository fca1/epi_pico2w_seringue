#include "wifi_manager.h"
#include "config_store.h"
#include "pico/cyw43_arch.h"
#include "pico/util/queue.h"
#include "FreeRTOS.h"
#include "task.h"
#include "lwip/tcp.h"
#include "lwip/apps/mdns.h"
#include "ws_crypto.h"
#include <stdio.h>
#include <string.h>

#define MAX_NETWORKS 16
typedef struct {char ssid[33];int16_t rssi;uint8_t auth;} network_t;
static device_config_t cfg;
static volatile wifi_state_t state;
static volatile bool connect_requested,scan_requested,scan_running;
static volatile uint32_t provisioning_until_ms;
static char ip[20],telemetry[448]="{\"state\":\"BOOT\"}",scan_json[2][900]={{"{\"scanning\":false,\"networks\":[]}"},{0}};static volatile uint8_t scan_json_active;
static network_t networks[MAX_NETWORKS];static volatile uint8_t network_count;
static queue_t commands;static struct tcp_pcb *ws_client;static uint8_t ws_rx[1024];static size_t ws_rx_len;
static bool mdns_initialized,mdns_registered;

static const char page[]=
"<!doctype html><html lang=fr><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
"<title>PasteDispenser</title><style>"
":root{color-scheme:dark;--bg:#07131f;--panel:#102335;--line:#29445b;--text:#edf7ff;--muted:#9bb2c4;--blue:#35a7ff;--green:#43d17c;--red:#ff5964}"
"*{box-sizing:border-box}body{margin:0;background:radial-gradient(circle at top,#173954 0,var(--bg) 52%);color:var(--text);font:16px system-ui,sans-serif;min-height:100vh}"
"main{width:min(760px,100%);margin:auto;padding:24px}header{display:flex;align-items:center;justify-content:space-between;gap:16px;margin-bottom:20px}"
"h1{font-size:clamp(1.6rem,5vw,2.3rem);margin:0}small,.label{color:var(--muted)}#link{display:flex;align-items:center;gap:8px;font-weight:700}.dot{width:10px;height:10px;border-radius:50%;background:var(--red);box-shadow:0 0 12px currentColor}"
".online .dot{background:var(--green)}.panel{background:linear-gradient(145deg,#142b40,var(--panel));border:1px solid var(--line);border-radius:18px;padding:18px;margin-bottom:16px;box-shadow:0 12px 30px #0005}"
".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(145px,1fr));gap:10px}.card{background:#081725aa;border:1px solid #203c52;border-radius:12px;padding:12px}.value{font-size:1.35rem;font-weight:750;margin-top:3px;overflow-wrap:anywhere}"
"h2{font-size:1rem;margin:0 0 14px;color:#cceaff;text-transform:uppercase;letter-spacing:.08em}.controls{display:grid;grid-template-columns:1fr 1fr 1fr;gap:10px}"
"button,input{font:inherit}button{min-height:58px;border:1px solid #60bdff55;border-radius:12px;background:linear-gradient(#258bd2,#176aa7);color:white;font-weight:750;cursor:pointer;box-shadow:0 5px 0 #0a456e,0 9px 16px #0005;transition:transform .08s,box-shadow .08s,filter .08s;touch-action:none;user-select:none}"
"button:hover{filter:brightness(1.12)}button:active,button.pressed{transform:translateY(5px);box-shadow:0 0 0 #0a456e,0 3px 7px #0007;filter:brightness(.88)}button:disabled{opacity:.45;cursor:not-allowed}.stop{background:linear-gradient(#f05661,#b62631);border-color:#ff9aa1;box-shadow:0 5px 0 #72131b,0 9px 16px #0005}"
".dose{display:flex;gap:10px;align-items:stretch}.dose label{flex:1}.dose input{width:100%;height:58px;margin-top:6px;border:1px solid var(--line);border-radius:12px;background:#071623;color:white;padding:0 14px;font-size:1.2rem}.dose button{min-width:130px;align-self:end}"
"#message{min-height:24px;margin-top:12px;color:var(--muted)}@media(max-width:520px){main{padding:16px}.controls{grid-template-columns:1fr 1fr}.stop{grid-column:1/-1}.dose{flex-direction:column}.dose button{width:100%}}"
"</style><main><header><div><h1>PasteDispenser</h1><small>Pousse-seringue connecté</small></div><div id=link><i class=dot></i><span>Hors ligne</span></div></header>"
"<section class=panel><h2>État de la seringue</h2><div id=status class=grid><div class=card><div class=label>Connexion</div><div class=value>En attente…</div></div></div></section>"
"<section class=panel><h2>Commande manuelle</h2><div class=controls><button id=p>Pousser</button><button id=t>Tirer</button><button id=q class=stop>Arrêt</button></div><div id=message></div></section>"
"<section class=panel><h2>Fourniture contrôlée</h2><div class=dose><label class=label>Course de fourniture (mm)<input id=d type=number value=.8 min=.01 step=.05 inputmode=decimal></label><button id=b>Doser</button></div></section>"
"</main><script>let w,retry;const $=id=>document.getElementById(id),labels={state:['État',''],position_mm:['Position',' mm'],remaining_course_mm:['Course restante',' mm'],activation_count:['Activations',''],sg_result:['StallGuard',''],load:['Charge',''],fault:['Défaut','']};"
"function connected(ok){$('link').className=ok?'online':'';$('link').querySelector('span').textContent=ok?'Connecté':'Hors ligne';for(const e of document.querySelectorAll('button'))e.disabled=!ok}"
"function render(v){$('status').innerHTML=Object.entries(labels).map(([k,a])=>`<div class=card><div class=label>${a[0]}</div><div class=value>${v[k]===undefined?'—':v[k]}${v[k]!==undefined?a[1]:''}</div></div>`).join('');$('message').textContent=v.fault?'Commande moteur indisponible : défaut '+v.fault:v.state==='READY'?'Prêt à recevoir une commande':'Mouvement : '+v.state}"
"function send(command,o={}){if(w&&w.readyState===1){w.send(JSON.stringify({command,...o}));return true}$('message').textContent='Commande non envoyée : connexion indisponible';return false}"
"function open(){clearTimeout(retry);w=new WebSocket('ws://'+location.host+'/ws');w.onopen=()=>connected(true);w.onmessage=e=>{try{render(JSON.parse(e.data))}catch(_){$('message').textContent='État reçu illisible'}};w.onerror=()=>w.close();w.onclose=()=>{connected(false);retry=setTimeout(open,1500)}}"
"function manual(button,start,stop){const release=()=>{button.classList.remove('pressed');send(stop)};button.addEventListener('pointerdown',e=>{e.preventDefault();button.setPointerCapture(e.pointerId);button.classList.add('pressed');send(start)});for(const name of ['pointerup','pointercancel','lostpointercapture'])button.addEventListener(name,release)}"
"manual($('p'),'push_start','push_stop');manual($('t'),'pull_start','pull_stop');$('q').onclick=()=>send('stop');$('b').onclick=()=>{const mm=+$('d').value;if(mm>0)send('dose',{distance_mm:mm,speed_mm_s:5,retract_mm:.1});else $('message').textContent='Saisir une course positive'};connected(false);open();"
"</script></html>";


static void enqueue_stop(void){machine_command_t c={.kind=CMD_STOP},drop;if(!queue_try_add(&commands,&c)){queue_try_remove(&commands,&drop);queue_try_add(&commands,&c);}}
static void ws_closed(void){if(ws_client){ws_client=NULL;enqueue_stop();}}
static void tcp_error(void *arg,err_t err){(void)arg;(void)err;ws_closed();}
static err_t http_sent(void*a,struct tcp_pcb*p,u16_t len){(void)a;(void)len;tcp_close(p);return ERR_OK;}
static void http_reply(struct tcp_pcb*p,const char*type,const char*body){char h[160];int n=snprintf(h,sizeof(h),"HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %u\r\nConnection: close\r\n\r\n",type,(unsigned)strlen(body));tcp_write(p,h,n,TCP_WRITE_FLAG_COPY);tcp_write(p,body,strlen(body),TCP_WRITE_FLAG_COPY);tcp_sent(p,http_sent);tcp_output(p);}

static bool websocket_upgrade(struct tcp_pcb*p,const char*req){const char*k=strstr(req,"Sec-WebSocket-Key:");if(!k)return false;k+=18;while(*k==' ')k++;const char*end=strstr(k,"\r\n");if(!end)return false;char accept[29];if(!ws_crypto_accept(k,(size_t)(end-k),accept))return false;char response[180];int n=snprintf(response,sizeof(response),"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: %s\r\n\r\n",accept);tcp_write(p,response,n,TCP_WRITE_FLAG_COPY);tcp_output(p);if(ws_client&&ws_client!=p)tcp_abort(ws_client);ws_client=p;tcp_err(p,tcp_error);return true;}

static bool websocket_data(const uint8_t*d,size_t n){if(ws_rx_len+n>sizeof(ws_rx))return false;memcpy(ws_rx+ws_rx_len,d,n);ws_rx_len+=n;size_t consumed=0;while(ws_rx_len-consumed>=2){uint8_t*frame=ws_rx+consumed;uint8_t opcode=frame[0]&0x0f;size_t len=frame[1]&0x7f,pos=2;if(len==126){if(ws_rx_len-consumed<4)break;len=((size_t)frame[2]<<8)|frame[3];pos=4;}else if(len==127)return false;if(!(frame[1]&0x80)||len>511)return false;size_t frame_len=pos+4+len;if(ws_rx_len-consumed<frame_len)break;if(opcode==8){enqueue_stop();return false;}if(opcode==1){const uint8_t*mask=frame+pos;pos+=4;char json[512];for(size_t i=0;i<len;i++)json[i]=(char)(frame[pos+i]^mask[i&3]);json[len]=0;machine_command_t c;if(command_parse_json(json,len,&c)){if(c.kind==CMD_STOP)enqueue_stop();else queue_try_add(&commands,&c);}}consumed+=frame_len;}if(consumed){memmove(ws_rx,ws_rx+consumed,ws_rx_len-consumed);ws_rx_len-=consumed;}return true;}

static err_t recv_cb(void*a,struct tcp_pcb*p,struct pbuf*b,err_t e){(void)a;if(!b){if(p==ws_client)ws_closed();tcp_close(p);return ERR_OK;}uint8_t reqbuf[1024];size_t n=b->tot_len<sizeof(reqbuf)-1?b->tot_len:sizeof(reqbuf)-1;pbuf_copy_partial(b,reqbuf,n,0);reqbuf[n]=0;tcp_recved(p,b->tot_len);pbuf_free(b);if(e!=ERR_OK){if(p==ws_client)ws_closed();tcp_abort(p);return ERR_ABRT;}if(p==ws_client){if(!websocket_data(reqbuf,n)){ws_closed();tcp_close(p);}return ERR_OK;}const char*req=(const char*)reqbuf;if(!strncmp(req,"GET /ws ",8)&&strstr(req,"Upgrade: websocket")){ws_rx_len=0;if(!websocket_upgrade(p,req))http_reply(p,"text/plain","Bad WebSocket request");}else if(!strncmp(req,"GET /api/status",15))http_reply(p,"application/json",telemetry);else if(!strncmp(req,"POST /api/command",17)){char*body=strstr(req,"\r\n\r\n");machine_command_t c;if(body&&command_parse_json(body+4,strlen(body+4),&c)&&queue_try_add(&commands,&c))http_reply(p,"application/json","{\"ok\":true}");else http_reply(p,"application/json","{\"ok\":false}");}else http_reply(p,"text/html; charset=utf-8",page);return ERR_OK;}
static err_t accept_cb(void*a,struct tcp_pcb*n,err_t e){(void)a;if(e!=ERR_OK)return e;tcp_recv(n,recv_cb);return ERR_OK;}
static void server_start(void){cyw43_arch_lwip_begin();struct tcp_pcb*p=tcp_new_ip_type(IPADDR_TYPE_ANY);if(p&&tcp_bind(p,NULL,80)==ERR_OK){p=tcp_listen_with_backlog(p,2);tcp_accept(p,accept_cb);}else if(p)tcp_close(p);cyw43_arch_lwip_end();}
static void mdns_http_txt(struct mdns_service*service,void*arg){(void)arg;mdns_resp_add_service_txtitem(service,"path=/",6);}
static void mdns_start(void){cyw43_arch_lwip_begin();if(!mdns_initialized){mdns_resp_init();mdns_initialized=true;}if(mdns_registered)mdns_resp_remove_netif(netif_default);netif_set_hostname(netif_default,"dispenser");if(mdns_resp_add_netif(netif_default,"dispenser")==ERR_OK){mdns_registered=true;mdns_resp_add_service(netif_default,"PasteDispenser","_http",DNSSD_PROTO_TCP,80,mdns_http_txt,NULL);}else mdns_registered=false;cyw43_arch_lwip_end();}

static int scan_result(void*env,const cyw43_ev_scan_result_t*r){(void)env;if(!r||!r->ssid_len)return 0;uint8_t count=network_count;for(uint8_t i=0;i<count;i++)if(strlen(networks[i].ssid)==r->ssid_len&&!memcmp(networks[i].ssid,r->ssid,r->ssid_len)){if(r->rssi>networks[i].rssi)networks[i].rssi=r->rssi;return 0;}if(count>=MAX_NETWORKS)return 0;network_t*n=&networks[count];memcpy(n->ssid,r->ssid,r->ssid_len);n->ssid[r->ssid_len]=0;n->rssi=r->rssi;n->auth=r->auth_mode;network_count=count+1;return 0;}
static void set_scan_json(const char*text){uint8_t next=scan_json_active^1u;snprintf(scan_json[next],sizeof(scan_json[next]),"%s",text);scan_json_active=next;}
static void format_scan_results(void){for(uint8_t i=0;i<network_count;i++)for(uint8_t j=i+1;j<network_count;j++)if(networks[j].rssi>networks[i].rssi){network_t t=networks[i];networks[i]=networks[j];networks[j]=t;}uint8_t next=scan_json_active^1u;char*out=scan_json[next];size_t used=snprintf(out,sizeof(scan_json[next]),"{\"scanning\":false,\"networks\":[");for(uint8_t i=0;i<network_count&&used<sizeof(scan_json[next])-64;i++){char escaped[67];size_t e=0;for(size_t j=0;networks[i].ssid[j]&&e<sizeof(escaped)-2;j++){char c=networks[i].ssid[j];if(c=='\"'||c=='\\')escaped[e++]='\\';if((unsigned char)c>=32)escaped[e++]=c;}escaped[e]=0;used+=snprintf(out+used,sizeof(scan_json[next])-used,"%s{\"ssid\":\"%s\",\"rssi\":%d,\"secure\":%s}",i?",":"",escaped,networks[i].rssi,networks[i].auth?"true":"false");}snprintf(out+used,sizeof(scan_json[next])-used,"]}");scan_json_active=next;}

static void network_task(void*arg){(void)arg;while(true){if(scan_requested&&!scan_running){scan_requested=false;scan_running=true;network_count=0;set_scan_json("{\"scanning\":true,\"networks\":[]}");cyw43_arch_enable_sta_mode();cyw43_wifi_scan_options_t opts={0};if(cyw43_wifi_scan(&cyw43_state,&opts,NULL,scan_result)){scan_running=false;format_scan_results();}}
 if(scan_running&&!cyw43_wifi_scan_active(&cyw43_state)){scan_running=false;format_scan_results();}
 if(connect_requested&&!scan_running){connect_requested=false;state=WIFI_CONNECTING;cyw43_arch_enable_sta_mode();int rc=cyw43_arch_wifi_connect_timeout_ms(cfg.wifi_ssid,cfg.wifi_password,CYW43_AUTH_WPA2_AES_PSK,20000);if(rc==0){state=WIFI_CONNECTED;provisioning_until_ms=0;snprintf(ip,sizeof(ip),"%s",ip4addr_ntoa(netif_ip4_addr(netif_default)));device_config_t saved;config_store_load(&saved);snprintf(saved.wifi_ssid,sizeof(saved.wifi_ssid),"%s",cfg.wifi_ssid);snprintf(saved.wifi_password,sizeof(saved.wifi_password),"%s",cfg.wifi_password);config_store_save(&saved);cfg=saved;server_start();mdns_start();}else state=rc==PICO_ERROR_TIMEOUT?WIFI_TIMEOUT:WIFI_AUTH_FAILED;}vTaskDelay(pdMS_TO_TICKS(25));}}

void wifi_manager_init(const device_config_t*c){cfg=*c;queue_init(&commands,sizeof(machine_command_t),4);state=WIFI_IDLE;if(!cfg.wifi_ssid[0])wifi_manager_open_provisioning(300000);configASSERT(xTaskCreate(network_task,"wifi",2048,NULL,3,NULL)==pdPASS);
#if DISPENSER_WIFI_AUTOCONNECT
 if(cfg.wifi_ssid[0])connect_requested=true;
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
void wifi_manager_publish(const char*j){size_t n=strlen(j);if(n>=sizeof(telemetry))n=sizeof(telemetry)-1;cyw43_arch_lwip_begin();memcpy(telemetry,j,n);telemetry[n]=0;if(ws_client){uint8_t frame[452];size_t pos=0;frame[pos++]=0x81;if(n<126)frame[pos++]=(uint8_t)n;else{frame[pos++]=126;frame[pos++]=(uint8_t)(n>>8);frame[pos++]=(uint8_t)n;}memcpy(frame+pos,telemetry,n);if(tcp_write(ws_client,frame,pos+n,TCP_WRITE_FLAG_COPY)==ERR_OK)tcp_output(ws_client);}cyw43_arch_lwip_end();}

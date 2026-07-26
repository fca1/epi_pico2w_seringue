#include "wifi_manager.h"
#include "config_store.h"
#include "pico/cyw43_arch.h"
#include "pico/multicore.h"
#include "pico/util/queue.h"
#include "lwip/tcp.h"
#include "ws_crypto.h"
#include <stdio.h>
#include <string.h>

#define MAX_NETWORKS 16
typedef struct {char ssid[33];int16_t rssi;uint8_t auth;} network_t;
static device_config_t cfg;
static volatile wifi_state_t state;
static volatile bool connect_requested,scan_requested,scan_running;
static volatile uint32_t provisioning_until_ms;
static char ip[20],telemetry[220]="{\"state\":\"BOOT\"}",scan_json[2][900]={{"{\"scanning\":false,\"networks\":[]}"},{0}};static volatile uint8_t scan_json_active;
static network_t networks[MAX_NETWORKS];static volatile uint8_t network_count;
static queue_t commands;static struct tcp_pcb *ws_client;static uint8_t ws_rx[1024];static size_t ws_rx_len;

static const char page[]="<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width'><title>PasteDispenser</title><style>body{font:18px system-ui;max-width:38rem;margin:auto;padding:2rem;background:#14202b;color:white}button,input{font:inherit;padding:.7rem;margin:.3rem}button{background:#1675d1;color:white;border:0;border-radius:.4rem}.stop{background:#b22}pre{white-space:pre-wrap}</style><h1>PasteDispenser</h1><pre id=s>Connexion...</pre><button id=p>Pousser</button><button id=t>Tirer</button><button class=stop onclick=x('stop')>Arrêter</button><p><input id=d type=number value=.8 step=.05> mm <button onclick=x('dose',{distance_mm:+d.value,speed_mm_s:5,retract_mm:.1})>Doser</button></p><script>let w;function open(){w=new WebSocket('ws://'+location.host+'/ws');w.onmessage=e=>s.textContent=e.data;w.onclose=()=>setTimeout(open,1000)}function x(command,o={}){if(w?.readyState===1)w.send(JSON.stringify({command,...o}))}for(let[b,a,z]of[[p,'push_start','push_stop'],[t,'pull_start','pull_stop']]){b.onpointerdown=()=>x(a);for(let e of['pointerup','pointercancel','lostpointercapture'])b.addEventListener(e,()=>x(z))}open()</script>";


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

static int scan_result(void*env,const cyw43_ev_scan_result_t*r){(void)env;if(!r||!r->ssid_len)return 0;uint8_t count=network_count;for(uint8_t i=0;i<count;i++)if(strlen(networks[i].ssid)==r->ssid_len&&!memcmp(networks[i].ssid,r->ssid,r->ssid_len)){if(r->rssi>networks[i].rssi)networks[i].rssi=r->rssi;return 0;}if(count>=MAX_NETWORKS)return 0;network_t*n=&networks[count];memcpy(n->ssid,r->ssid,r->ssid_len);n->ssid[r->ssid_len]=0;n->rssi=r->rssi;n->auth=r->auth_mode;network_count=count+1;return 0;}
static void set_scan_json(const char*text){uint8_t next=scan_json_active^1u;snprintf(scan_json[next],sizeof(scan_json[next]),"%s",text);scan_json_active=next;}
static void format_scan_results(void){for(uint8_t i=0;i<network_count;i++)for(uint8_t j=i+1;j<network_count;j++)if(networks[j].rssi>networks[i].rssi){network_t t=networks[i];networks[i]=networks[j];networks[j]=t;}uint8_t next=scan_json_active^1u;char*out=scan_json[next];size_t used=snprintf(out,sizeof(scan_json[next]),"{\"scanning\":false,\"networks\":[");for(uint8_t i=0;i<network_count&&used<sizeof(scan_json[next])-64;i++){char escaped[67];size_t e=0;for(size_t j=0;networks[i].ssid[j]&&e<sizeof(escaped)-2;j++){char c=networks[i].ssid[j];if(c=='\"'||c=='\\')escaped[e++]='\\';if((unsigned char)c>=32)escaped[e++]=c;}escaped[e]=0;used+=snprintf(out+used,sizeof(scan_json[next])-used,"%s{\"ssid\":\"%s\",\"rssi\":%d,\"secure\":%s}",i?",":"",escaped,networks[i].rssi,networks[i].auth?"true":"false");}snprintf(out+used,sizeof(scan_json[next])-used,"]}");scan_json_active=next;}

static void network_core(void){while(true){if(scan_requested&&!scan_running){scan_requested=false;scan_running=true;network_count=0;set_scan_json("{\"scanning\":true,\"networks\":[]}");cyw43_arch_enable_sta_mode();cyw43_wifi_scan_options_t opts={0};if(cyw43_wifi_scan(&cyw43_state,&opts,NULL,scan_result)){scan_running=false;format_scan_results();}}
 if(scan_running&&!cyw43_wifi_scan_active(&cyw43_state)){scan_running=false;format_scan_results();}
 if(connect_requested&&!scan_running){connect_requested=false;state=WIFI_CONNECTING;cyw43_arch_enable_sta_mode();int rc=cyw43_arch_wifi_connect_timeout_ms(cfg.wifi_ssid,cfg.wifi_password,CYW43_AUTH_WPA2_AES_PSK,20000);if(rc==0){state=WIFI_CONNECTED;provisioning_until_ms=0;snprintf(ip,sizeof(ip),"%s",ip4addr_ntoa(netif_ip4_addr(netif_default)));config_store_save(&cfg);server_start();}else state=rc==PICO_ERROR_TIMEOUT?WIFI_TIMEOUT:WIFI_AUTH_FAILED;}sleep_ms(25);}}

void wifi_manager_init(const device_config_t*c){cfg=*c;queue_init(&commands,sizeof(machine_command_t),4);state=WIFI_IDLE;if(!cfg.wifi_ssid[0])wifi_manager_open_provisioning(300000);multicore_launch_core1(network_core);if(cfg.wifi_ssid[0])connect_requested=true;}
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
void wifi_manager_publish(const char*j){size_t n=strlen(j);if(n>=sizeof(telemetry))n=sizeof(telemetry)-1;cyw43_arch_lwip_begin();memcpy(telemetry,j,n);telemetry[n]=0;if(ws_client){uint8_t frame[224];size_t pos=0;frame[pos++]=0x81;if(n<126)frame[pos++]=(uint8_t)n;else{frame[pos++]=126;frame[pos++]=(uint8_t)(n>>8);frame[pos++]=(uint8_t)n;}memcpy(frame+pos,telemetry,n);if(tcp_write(ws_client,frame,pos+n,TCP_WRITE_FLAG_COPY)==ERR_OK)tcp_output(ws_client);}cyw43_arch_lwip_end();}

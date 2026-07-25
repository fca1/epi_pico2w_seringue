#include "wifi_manager.h"
#include "config_store.h"
#include "pico/cyw43_arch.h"
#include "pico/multicore.h"
#include "pico/util/queue.h"
#include "lwip/tcp.h"
#include <stdio.h>
#include <string.h>
static device_config_t cfg;static volatile wifi_state_t state;static volatile bool request;
static volatile uint32_t provisioning_until_ms;
static char ip[20],telemetry[180]="{\"state\":\"BOOT\"}";static queue_t commands;
static const char page[]="<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width'><title>PasteDispenser</title><style>body{font:18px system-ui;max-width:38rem;margin:auto;padding:2rem;background:#14202b;color:white}button,input{font:inherit;padding:.7rem;margin:.3rem}button{background:#1675d1;color:white;border:0;border-radius:.4rem}.stop{background:#b22}pre{white-space:pre-wrap}</style><h1>PasteDispenser</h1><pre id=s>Connexion...</pre><button id=p>Pousser</button><button id=t>Tirer</button><button class=stop onclick=x('stop')>Arrêter</button><p><input id=d type=number value=.8 step=.05> mm <button onclick=x('dose',{distance_mm:+d.value,speed_mm_s:5,retract_mm:.1})>Doser</button></p><script>async function x(command,o={}){await fetch('/api/command',{method:'POST',body:JSON.stringify({command,...o})})}for(let [b,a,z] of [[p,'push_start','push_stop'],[t,'pull_start','pull_stop']]){b.onpointerdown=()=>x(a);for(let e of ['pointerup','pointercancel'])b.addEventListener(e,()=>x(z))}setInterval(async()=>s.textContent=await(await fetch('/api/status')).text(),500)</script>";
static err_t sent(void*a,struct tcp_pcb*p,u16_t len){(void)a;(void)len;tcp_close(p);return ERR_OK;}
static void reply(struct tcp_pcb*p,const char*type,const char*body){char h[160];int n=snprintf(h,sizeof(h),"HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %u\r\nConnection: close\r\n\r\n",type,(unsigned)strlen(body));tcp_write(p,h,n,TCP_WRITE_FLAG_COPY);tcp_write(p,body,strlen(body),TCP_WRITE_FLAG_COPY);tcp_sent(p,sent);tcp_output(p);}
static err_t recv_cb(void*a,struct tcp_pcb*p,struct pbuf*b,err_t e){(void)a;if(!b){tcp_close(p);return ERR_OK;}char reqbuf[600];size_t n=b->tot_len<sizeof(reqbuf)-1?b->tot_len:sizeof(reqbuf)-1;pbuf_copy_partial(b,reqbuf,n,0);reqbuf[n]=0;tcp_recved(p,b->tot_len);pbuf_free(b);
 if(e!=ERR_OK){tcp_abort(p);return ERR_ABRT;}if(!strncmp(reqbuf,"GET /api/status",15))reply(p,"application/json",telemetry);else if(!strncmp(reqbuf,"POST /api/command",17)){char*body=strstr(reqbuf,"\r\n\r\n");machine_command_t c;if(body&&command_parse_json(body+4,strlen(body+4),&c)&&queue_try_add(&commands,&c))reply(p,"application/json","{\"ok\":true}");else reply(p,"application/json","{\"ok\":false}");}else reply(p,"text/html; charset=utf-8",page);return ERR_OK;}
static err_t accept_cb(void*a,struct tcp_pcb*n,err_t e){(void)a;if(e!=ERR_OK)return e;tcp_recv(n,recv_cb);return ERR_OK;}
static void server_start(void){cyw43_arch_lwip_begin();struct tcp_pcb*p=tcp_new_ip_type(IPADDR_TYPE_ANY);if(p&&tcp_bind(p,NULL,80)==ERR_OK){p=tcp_listen_with_backlog(p,2);tcp_accept(p,accept_cb);}else if(p)tcp_close(p);cyw43_arch_lwip_end();}
static void network_core(void){while(true){if(!request){sleep_ms(50);continue;}request=false;state=WIFI_CONNECTING;cyw43_arch_enable_sta_mode();int rc=cyw43_arch_wifi_connect_timeout_ms(cfg.wifi_ssid,cfg.wifi_password,CYW43_AUTH_WPA2_AES_PSK,20000);if(rc==0){state=WIFI_CONNECTED;provisioning_until_ms=0;snprintf(ip,sizeof(ip),"%s",ip4addr_ntoa(netif_ip4_addr(netif_default)));config_store_save(&cfg);server_start();}else state=rc==PICO_ERROR_TIMEOUT?WIFI_TIMEOUT:WIFI_AUTH_FAILED;}}
void wifi_manager_init(const device_config_t*c){cfg=*c;queue_init(&commands,sizeof(machine_command_t),4);state=WIFI_IDLE;if(!cfg.wifi_ssid[0])wifi_manager_open_provisioning(300000);multicore_launch_core1(network_core);if(cfg.wifi_ssid[0])request=true;}
void wifi_manager_open_provisioning(uint32_t duration_ms){provisioning_until_ms=to_ms_since_boot(get_absolute_time())+duration_ms;}
bool wifi_manager_provisioning_open(void){return provisioning_until_ms&&((int32_t)(provisioning_until_ms-to_ms_since_boot(get_absolute_time()))>0);}
bool wifi_manager_set_ssid(const uint8_t*d,size_t n){if(n>32)return false;memcpy(cfg.wifi_ssid,d,n);cfg.wifi_ssid[n]=0;return true;}bool wifi_manager_set_password(const uint8_t*d,size_t n){if(n>64)return false;memcpy(cfg.wifi_password,d,n);cfg.wifi_password[n]=0;return true;}
bool wifi_manager_request_connect(void){if(!cfg.wifi_ssid[0]||state==WIFI_CONNECTING)return false;request=true;return true;}wifi_state_t wifi_manager_state(void){return state;}
const char*wifi_manager_state_name(void){static const char*n[]={"IDLE","CONNECTING","CONNECTED","AUTH_FAILED","NETWORK_NOT_FOUND","TIMEOUT"};return n[state];}const char*wifi_manager_ip(void){return ip;}
bool wifi_manager_take_command(machine_command_t*c){return queue_try_remove(&commands,c);}void wifi_manager_publish(const char*j){size_t n=strlen(j);if(n>=sizeof(telemetry))n=sizeof(telemetry)-1;memcpy(telemetry,j,n);telemetry[n]=0;}

#include "wifi_uart_command.h"
#include <string.h>

bool wifi_uart_parse(const char*text,size_t length,char ssid[33],char password[65]){
 if(!text||!ssid||!password||length<15||length>111||memcmp(text,"WIFI:",5))return false;
 const char*separator=NULL;for(size_t i=5;i+10<=length;i++)if(!memcmp(text+i,";PASSWORD:",10)){separator=text+i;break;}
 if(!separator)return false;size_t ssid_len=(size_t)(separator-(text+5)),password_len=length-(size_t)(separator+10-text);
 while(password_len&&(separator[10+password_len-1]=='\r'||separator[10+password_len-1]=='\n'))password_len--;
 if(!ssid_len||ssid_len>32||password_len>64)return false;
 memcpy(ssid,text+5,ssid_len);ssid[ssid_len]=0;memcpy(password,separator+10,password_len);password[password_len]=0;return true;
}

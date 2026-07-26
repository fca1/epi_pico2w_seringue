#include "ble_service.h"
#include "btstack.h"
#include "pico/cyw43_arch.h"
#include "paste_dispenser.h"
#include "wifi_manager.h"
#include <string.h>
static hci_con_handle_t connection=HCI_CON_HANDLE_INVALID;
static machine_command_t pending;static volatile bool has_command;
static char status_json[180]="{\"state\":\"BOOT\"}";static uint16_t status_len;static volatile bool notify_pending;
static btstack_packet_callback_registration_t hci_event_callback;
static uint16_t read_cb(hci_con_handle_t c,uint16_t handle,uint16_t offset,uint8_t*buffer,uint16_t size){(void)c;
 if(handle==ATT_CHARACTERISTIC_7e400003_b5a3_f393_e0a9_e50e24dcca9e_01_VALUE_HANDLE)return att_read_callback_handle_blob((const uint8_t*)status_json,status_len,offset,buffer,size);
 if(handle==ATT_CHARACTERISTIC_7e400007_b5a3_f393_e0a9_e50e24dcca9e_01_VALUE_HANDLE){const char*s=wifi_manager_state_name();return att_read_callback_handle_blob((const uint8_t*)s,strlen(s),offset,buffer,size);}
 if(handle==ATT_CHARACTERISTIC_7e400008_b5a3_f393_e0a9_e50e24dcca9e_01_VALUE_HANDLE){const char*s=wifi_manager_ip();return att_read_callback_handle_blob((const uint8_t*)s,strlen(s),offset,buffer,size);}
 if(handle==ATT_CHARACTERISTIC_7e40000a_b5a3_f393_e0a9_e50e24dcca9e_01_VALUE_HANDLE){const char*s=wifi_manager_scan_results();return att_read_callback_handle_blob((const uint8_t*)s,strlen(s),offset,buffer,size);}
 if(handle==ATT_CHARACTERISTIC_GAP_APPEARANCE_01_VALUE_HANDLE){uint16_t appearance=0;return att_read_callback_handle_little_endian_16(appearance,offset,buffer,size);}
 return 0;}
static int write_cb(hci_con_handle_t c,uint16_t handle,uint16_t mode,uint16_t offset,uint8_t*buffer,uint16_t size){(void)c;(void)mode;if(offset)return ATT_ERROR_INVALID_OFFSET;
 if(handle==ATT_CHARACTERISTIC_7e400002_b5a3_f393_e0a9_e50e24dcca9e_01_VALUE_HANDLE){machine_command_t cmd;if(!command_parse_json((const char*)buffer,size,&cmd))return ATT_ERROR_VALUE_NOT_ALLOWED;if(has_command&&cmd.kind!=CMD_STOP)return ATT_ERROR_WRITE_REQUEST_REJECTED;pending=cmd;has_command=true;return 0;}
 if(handle==ATT_CHARACTERISTIC_7e400004_b5a3_f393_e0a9_e50e24dcca9e_01_VALUE_HANDLE)return wifi_manager_provisioning_open()&&wifi_manager_set_ssid(buffer,size)?0:ATT_ERROR_WRITE_REQUEST_REJECTED;
 if(handle==ATT_CHARACTERISTIC_7e400005_b5a3_f393_e0a9_e50e24dcca9e_01_VALUE_HANDLE)return wifi_manager_provisioning_open()&&wifi_manager_set_password(buffer,size)?0:ATT_ERROR_WRITE_REQUEST_REJECTED;
 if(handle==ATT_CHARACTERISTIC_7e400006_b5a3_f393_e0a9_e50e24dcca9e_01_VALUE_HANDLE)return wifi_manager_provisioning_open()&&wifi_manager_request_connect()?0:ATT_ERROR_WRITE_REQUEST_REJECTED;
 if(handle==ATT_CHARACTERISTIC_7e400009_b5a3_f393_e0a9_e50e24dcca9e_01_VALUE_HANDLE)return wifi_manager_provisioning_open()&&wifi_manager_request_scan()?0:ATT_ERROR_WRITE_REQUEST_REJECTED;
 return 0;}
static void packet_handler(uint8_t type,uint16_t channel,uint8_t*packet,uint16_t size){(void)channel;(void)size;if(type!=HCI_EVENT_PACKET)return;switch(hci_event_packet_get_type(packet)){
 case HCI_EVENT_META_GAP:if(hci_event_gap_meta_get_subevent_code(packet)==GAP_SUBEVENT_LE_CONNECTION_COMPLETE)connection=gap_subevent_le_connection_complete_get_connection_handle(packet);break;
 case ATT_EVENT_CAN_SEND_NOW:if(connection!=HCI_CON_HANDLE_INVALID&&notify_pending){notify_pending=false;att_server_notify(connection,ATT_CHARACTERISTIC_7e400003_b5a3_f393_e0a9_e50e24dcca9e_01_VALUE_HANDLE,(uint8_t*)status_json,status_len);}break;
 case HCI_EVENT_DISCONNECTION_COMPLETE:connection=HCI_CON_HANDLE_INVALID;pending=(machine_command_t){CMD_STOP};has_command=true;break;default:break;}}
bool ble_service_init(void){if(cyw43_arch_init())return false;l2cap_init();sm_init();att_server_init(profile_data,read_cb,write_cb);hci_event_callback.callback=&packet_handler;hci_add_event_handler(&hci_event_callback);att_server_register_packet_handler(packet_handler);
 uint8_t adv[]={2,BLUETOOTH_DATA_TYPE_FLAGS,0x06,17,BLUETOOTH_DATA_TYPE_COMPLETE_LIST_OF_128_BIT_SERVICE_CLASS_UUIDS,0x9e,0xca,0xdc,0x24,0x0e,0xe5,0xa9,0xe0,0x93,0xf3,0xa3,0xb5,0x01,0x00,0x40,0x7e};
 uint8_t scan[]={22,BLUETOOTH_DATA_TYPE_COMPLETE_LOCAL_NAME,'P','a','s','t','e','D','i','s','p','e','n','s','e','r','-','P','i','c','o','2','W'};
 gap_advertisements_set_params(800,800,0,0,0,0,0);gap_advertisements_set_data(sizeof(adv),adv);gap_scan_response_set_data(sizeof(scan),scan);gap_advertisements_enable(1);hci_power_control(HCI_POWER_ON);return true;}
bool ble_service_take_command(machine_command_t*out){if(!has_command)return false;*out=pending;has_command=false;return true;}
void ble_service_publish(const char*json){size_t n=strlen(json);if(n>=sizeof(status_json))n=sizeof(status_json)-1;memcpy(status_json,json,n);status_json[n]=0;status_len=n;if(connection!=HCI_CON_HANDLE_INVALID&&!notify_pending){notify_pending=true;att_server_request_can_send_now_event(connection);}}
bool ble_service_connected(void){return connection!=HCI_CON_HANDLE_INVALID;}

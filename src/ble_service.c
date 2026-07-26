#include "ble_service.h"
#include "btstack.h"
#include "pico/async_context.h"
#include "pico/cyw43_arch.h"
#include "paste_dispenser.h"
#include "wifi_manager.h"
#include <string.h>
static hci_con_handle_t connection=HCI_CON_HANDLE_INVALID;
static volatile bool operational;static volatile uint8_t adv_params_status=255,adv_enable_status=255;
static machine_command_t pending;static volatile bool has_command;
static char status_json[448]="{\"state\":\"BOOT\"}";static uint16_t status_len;static volatile bool notify_pending;
static btstack_packet_callback_registration_t hci_event_callback;
static const uint8_t adv_data[]={2,BLUETOOTH_DATA_TYPE_FLAGS,0x06,17,BLUETOOTH_DATA_TYPE_COMPLETE_LIST_OF_128_BIT_SERVICE_CLASS_UUIDS,0x9e,0xca,0xdc,0x24,0x0e,0xe5,0xa9,0xe0,0x93,0xf3,0xa3,0xb5,0x01,0x00,0x40,0x7e};
static const uint8_t scan_response_data[]={22,BLUETOOTH_DATA_TYPE_COMPLETE_LOCAL_NAME,'P','a','s','t','e','D','i','s','p','e','n','s','e','r','-','P','i','c','o','2','W'};
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
 /* Scanning is read-only and must remain available after the provisioning
    window closes. Only credential writes and connection requests are gated. */
 if(handle==ATT_CHARACTERISTIC_7e400009_b5a3_f393_e0a9_e50e24dcca9e_01_VALUE_HANDLE)return wifi_manager_request_scan()?0:ATT_ERROR_WRITE_REQUEST_REJECTED;
 return 0;}
static void packet_handler(uint8_t type,uint16_t channel,uint8_t*packet,uint16_t size){(void)channel;(void)size;if(type!=HCI_EVENT_PACKET)return;switch(hci_event_packet_get_type(packet)){
 case BTSTACK_EVENT_STATE:if(btstack_event_state_get_state(packet)==HCI_STATE_WORKING){static bd_addr_t null_addr={0};gap_advertisements_set_params(0x0030,0x0030,0,0,null_addr,0x07,0x00);gap_advertisements_set_data(sizeof(adv_data),(uint8_t*)adv_data);gap_scan_response_set_data(sizeof(scan_response_data),(uint8_t*)scan_response_data);gap_advertisements_enable(1);operational=true;}break;
 case HCI_EVENT_COMMAND_COMPLETE:{uint16_t opcode=hci_event_command_complete_get_command_opcode(packet);uint8_t status=hci_event_command_complete_get_return_parameters(packet)[0];if(opcode==HCI_OPCODE_HCI_LE_SET_ADVERTISING_PARAMETERS)adv_params_status=status;else if(opcode==HCI_OPCODE_HCI_LE_SET_ADVERTISE_ENABLE)adv_enable_status=status;}break;
 case HCI_EVENT_META_GAP:if(hci_event_gap_meta_get_subevent_code(packet)==GAP_SUBEVENT_LE_CONNECTION_COMPLETE)connection=gap_subevent_le_connection_complete_get_connection_handle(packet);break;
 case ATT_EVENT_CAN_SEND_NOW:if(connection!=HCI_CON_HANDLE_INVALID&&notify_pending){notify_pending=false;att_server_notify(connection,ATT_CHARACTERISTIC_7e400003_b5a3_f393_e0a9_e50e24dcca9e_01_VALUE_HANDLE,(uint8_t*)status_json,status_len);}break;
 case HCI_EVENT_DISCONNECTION_COMPLETE:connection=HCI_CON_HANDLE_INVALID;pending=(machine_command_t){CMD_STOP};has_command=true;gap_advertisements_enable(1);break;default:break;}}
bool ble_service_init(void){if(cyw43_arch_init())return false;l2cap_init();sm_init();att_server_init(profile_data,read_cb,write_cb);hci_event_callback.callback=&packet_handler;hci_add_event_handler(&hci_event_callback);att_server_register_packet_handler(packet_handler);hci_power_control(HCI_POWER_ON);return true;}
bool ble_service_take_command(machine_command_t*out){if(!has_command)return false;*out=pending;has_command=false;return true;}
typedef struct{const char*json;size_t length;} publish_request_t;
static uint32_t publish_in_btstack_context(void*arg){publish_request_t*r=arg;memcpy(status_json,r->json,r->length);status_json[r->length]=0;status_len=(uint16_t)r->length;if(connection!=HCI_CON_HANDLE_INVALID&&!notify_pending){notify_pending=true;att_server_request_can_send_now_event(connection);}return 0;}
void ble_service_publish(const char*json){size_t n=strlen(json);if(n>=sizeof(status_json))n=sizeof(status_json)-1;publish_request_t request={json,n};async_context_execute_sync(cyw43_arch_async_context(),publish_in_btstack_context,&request);}
bool ble_service_connected(void){return connection!=HCI_CON_HANDLE_INVALID;}
bool ble_service_operational(void){return operational;}
uint8_t ble_service_advertising_status(void){return adv_params_status?adv_params_status:adv_enable_status;}

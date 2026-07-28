#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "hardware/gpio.h"
#include "hardware/watchdog.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include "app_tasks.h"
#include "app_state.h"
#include "board_config.h"
#include "buttons.h"
#include "command_api.h"
#include "config_store.h"
#include "firmware_version.h"
#include "motor_control.h"
#include "safety.h"
#include "stallguard_calibration.h"
#include "statistics.h"
#include "status_led.h"
#if DISPENSER_HAS_RADIO
#include "ble_service.h"
#include "wifi_manager.h"
#endif

#define COMMAND_QUEUE_LENGTH 8
#define TELEMETRY_SIZE 448
typedef struct{app_state_t state;float position_mm,remaining_mm,used_mm,trigger_dose_mm;uint32_t activation_count,sg_samples,fault;uint16_t sg_result;uint8_t load;bool sg_calibrating;const char*unload_result;}telemetry_snapshot_t;

static device_config_t config;
static motor_config_t motor_cfg;
static app_machine_t machine;
static safety_t safety;
static sg_calibration_t sg_cal;
static QueueHandle_t command_queue;
static telemetry_snapshot_t telemetry_snapshot;
static volatile bool motor_moving,radio_connected;
static int direction;
static uint32_t phase_ms;
static float retract_mm;
static const char *unload_result="none";
static void format_telemetry(char *json,size_t capacity,const telemetry_snapshot_t *s);
#if DISPENSER_HAS_RADIO
static bool radio_ready;

static void radio_init_task(void *arg){
 (void)arg;
 bool provision_at_boot=!gpio_get(PIN_SW_PULL);
 radio_ready=ble_service_init();
 status_led_init(radio_ready);
 if(radio_ready){
  wifi_manager_init(&config);
  if(provision_at_boot||!DISPENSER_WIFI_AUTOCONNECT)wifi_manager_open_provisioning(300000);
 }
 vTaskDelete(NULL);
}
#endif

static bool submit_command(const machine_command_t *command){
 if(command->kind==CMD_STOP){xQueueReset(command_queue);return xQueueSendToFront(command_queue,command,0)==pdTRUE;}
 return xQueueSendToBack(command_queue,command,0)==pdTRUE;
}

static void set_fault(safety_fault_t f){motor_stop();machine.fault_code=f;app_state_dispatch(&machine,EVT_FAULT);direction=0;}
static bool begin_manual(int d,uint32_t now){if(machine.state!=APP_READY||!motor_manual(d))return false;direction=d;safety_manual_started(&safety,now);app_state_dispatch(&machine,d>0?EVT_PUSH:EVT_PULL);if(d>0)statistics_increment();return true;}
static void stop_motion(void){if(machine.state==APP_HOMING)unload_result="stopped";motor_stop();direction=0;if(machine.state!=APP_FAULT)app_state_dispatch(&machine,EVT_STOP);}

static bool apply_config_value(config_param_t parameter,float value){device_config_t next=config;
 switch(parameter){
 case CFG_SCREW_PITCH_MM:next.screw_pitch_mm=value;break;case CFG_MOTOR_STEPS_PER_REV:next.motor_steps_per_rev=(uint16_t)value;break;case CFG_MICROSTEPS:next.microsteps=(uint16_t)value;break;
 case CFG_RUN_CURRENT_MA:next.motor_run_current_mA=(uint16_t)value;break;case CFG_HOLD_CURRENT_MA:next.motor_hold_current_mA=(uint16_t)value;break;
 case CFG_MANUAL_SPEED_MM_S:next.manual_speed_mm_s=value;break;case CFG_DOSING_SPEED_MM_S:next.dosing_speed_mm_s=value;break;case CFG_TRIGGER_DOSE_MM:next.trigger_dose_mm=value;break;
 case CFG_A1_MM_S2:next.a1_mm_s2=value;break;case CFG_AMAX_MM_S2:next.amax_mm_s2=value;break;case CFG_DMAX_MM_S2:next.dmax_mm_s2=value;break;case CFG_D1_MM_S2:next.d1_mm_s2=value;break;
 case CFG_RETRACT_DISTANCE_MM:next.retract_distance_mm=value;break;case CFG_RETRACT_SPEED_MM_S:next.retract_speed_mm_s=value;break;
 case CFG_RETRACT_DELAY_MS:next.retract_delay_ms=(uint32_t)value;break;case CFG_POSITION_MIN_MM:next.position_min_mm=value;break;case CFG_POSITION_MAX_MM:next.position_max_mm=value;break;
 case CFG_MANUAL_TIMEOUT_MS:next.manual_timeout_ms=(uint32_t)value;break;case CFG_STALLGUARD_THRESHOLD:next.stallguard_threshold=(int8_t)value;break;
 case CFG_STALLGUARD_WARNING:next.stallguard_warning_level=(uint16_t)value;break;case CFG_STALLGUARD_CRITICAL:next.stallguard_critical_level=(uint16_t)value;break;
 case CFG_STALLGUARD_FILTER_COUNT:next.stallguard_filter_count=(uint16_t)value;break;case CFG_STALLGUARD_ENABLED:next.stallguard_enabled=value!=0;break;default:return false;}
 motor_config_t candidate={.motor_steps_per_rev=next.motor_steps_per_rev,.microsteps=next.microsteps,.motor_run_current_mA=next.motor_run_current_mA,.motor_hold_current_mA=next.motor_hold_current_mA,.screw_pitch_mm=next.screw_pitch_mm,.manual_speed_mm_s=next.manual_speed_mm_s,.a1_mm_s2=next.a1_mm_s2,.amax_mm_s2=next.amax_mm_s2,.dmax_mm_s2=next.dmax_mm_s2,.d1_mm_s2=next.d1_mm_s2};
 if(!device_config_validate(&next)||!motor_config_valid(&candidate)||!config_store_save(&next))return false;config=next;return true;}

static void execute(machine_command_t *c,uint32_t now){
 if(c->kind==CMD_STOP||c->kind==CMD_PUSH_STOP||c->kind==CMD_PULL_STOP){stop_motion();return;}
 if(c->kind==CMD_BOOTSEL){motor_stop();stdio_flush();vTaskDelay(pdMS_TO_TICKS(50));rom_reset_usb_boot(0,0);return;}
 if(c->kind==CMD_REBOOT){motor_stop();watchdog_reboot(0,0,10);return;}
 if(c->kind==CMD_RESET&&machine.state==APP_FAULT){safety_init(&safety);app_state_dispatch(&machine,EVT_RESET);return;}
 if(c->kind==CMD_SG_CAL_CANCEL){sg_cal.active=false;motor_configure_stallguard(config.stallguard_enabled,config.stallguard_threshold);return;}
 if(c->kind==CMD_SG_CAL_START&&machine.state==APP_READY){sg_calibration_start(&sg_cal);motor_configure_stallguard(true,config.stallguard_threshold);return;}
 if(c->kind==CMD_SG_CAL_FINISH&&machine.state==APP_READY){uint16_t baseline,warning,critical;if(sg_calibration_finish(&sg_cal,&baseline,&warning,&critical)){config.stallguard_baseline=baseline;config.stallguard_warning_level=warning;config.stallguard_critical_level=critical;config.stallguard_calibration_speed_mm_s=config.manual_speed_mm_s;config.stallguard_enabled=1;config_store_save(&config);motor_configure_stallguard(true,config.stallguard_threshold);}return;}
 if(c->kind==CMD_SET_TRIGGER_DOSE){if(c->distance_mm>0&&c->distance_mm<=100){config.trigger_dose_mm=c->distance_mm;config_store_save(&config);}return;}
 if(c->kind==CMD_SET_CONFIG){apply_config_value(c->parameter,c->value);return;}
 if(c->kind==CMD_FLUSH_STATISTICS&&machine.state==APP_READY){statistics_flush();statistics_persist();return;}
 if(machine.state!=APP_READY)return;
 switch(c->kind){
 case CMD_PUSH_START:begin_manual(1,now);break;
 case CMD_PULL_START:begin_manual(-1,now);break;
 case CMD_UNLOAD_SYRINGE:{float pos=motor_microsteps_to_mm(&motor_cfg,motor_position_steps());if(pos<=config.position_min_mm){motor_set_position_mm(config.position_min_mm);unload_result="position_min";}else if(motor_move_relative(config.position_min_mm-pos,c->speed_mm_s>0?c->speed_mm_s:config.manual_speed_mm_s)){direction=-1;retract_mm=0;unload_result="running";safety_init(&safety);safety_manual_started(&safety,now);app_state_dispatch(&machine,EVT_HOME);}break;}
 case CMD_SET_ZERO:motor_set_zero();break;
 case CMD_MOVE_RELATIVE:if(motor_move_relative(c->distance_mm,c->speed_mm_s>0?c->speed_mm_s:config.dosing_speed_mm_s)){direction=c->distance_mm>=0?1:-1;retract_mm=0;app_state_dispatch(&machine,EVT_DOSE);if(c->distance_mm>0)statistics_increment();}break;
 case CMD_DOSE:if(c->distance_mm>0&&motor_move_relative(c->distance_mm,c->speed_mm_s>0?c->speed_mm_s:config.dosing_speed_mm_s)){direction=1;retract_mm=c->retract_mm>=0?c->retract_mm:config.retract_distance_mm;app_state_dispatch(&machine,EVT_DOSE);statistics_increment();}break;
 default:break;
 }
}

static void update_telemetry(void){
 telemetry_snapshot_t snapshot={0};
 float pos=motor_microsteps_to_mm(&motor_cfg,motor_position_steps()),remaining=config.position_max_mm-pos,used=pos-config.position_min_mm;
 if(remaining<0)remaining=0;if(used<0)used=0;
 snapshot.state=machine.state;snapshot.position_mm=pos;snapshot.remaining_mm=remaining;snapshot.used_mm=used;snapshot.trigger_dose_mm=config.trigger_dose_mm;snapshot.activation_count=statistics_activation_count();snapshot.sg_samples=sg_cal.samples;snapshot.fault=machine.fault_code;snapshot.sg_result=safety.sg_filtered;snapshot.load=safety.load_state;snapshot.sg_calibrating=sg_cal.active;snapshot.unload_result=unload_result;
 taskENTER_CRITICAL();telemetry_snapshot=snapshot;taskEXIT_CRITICAL();
}

static void motor_task(void *arg){
 (void)arg;button_state_t old={0};uint32_t conflict_since=0,last_status=0;TickType_t wake=xTaskGetTickCount();
 for(;;){uint32_t now=to_ms_since_boot(get_absolute_time());button_state_t b=buttons_update(now);machine_command_t command;
  while(xQueueReceive(command_queue,&command,0)==pdTRUE)execute(&command,now);
  if(b.conflict){if(!conflict_since)conflict_since=now;set_fault(SAFETY_BUTTON_CONFLICT);if(now-conflict_since>=5000){motor_stop();config_store_factory_reset();statistics_factory_reset();watchdog_reboot(0,0,0);}}
  else{conflict_since=0;if(machine.state!=APP_FAULT){
   if(b.push&&!old.push)begin_manual(1,now);else if(b.pull&&!old.pull)begin_manual(-1,now);else if(b.dose&&!old.dose){machine_command_t trigger={.kind=CMD_DOSE,.distance_mm=config.trigger_dose_mm,.speed_mm_s=config.dosing_speed_mm_s,.retract_mm=config.retract_distance_mm};execute(&trigger,now);}
   else if(!b.push&&!b.pull&&(machine.state==APP_MANUAL_PUSH||machine.state==APP_MANUAL_PULL))stop_motion();
   if(machine.state==APP_STOPPING&&motor_is_stopped())app_state_dispatch(&machine,EVT_STOPPED);
   if(machine.state==APP_DOSING&&motor_target_reached()){direction=0;if(retract_mm>0){app_state_dispatch(&machine,EVT_MOVE_DONE);phase_ms=now;}else{app_state_dispatch(&machine,EVT_MOVE_DONE);app_state_dispatch(&machine,EVT_MOVE_DONE);}}
   if(machine.state==APP_RETRACTING&&direction==0&&now-phase_ms>=config.retract_delay_ms){if(motor_move_relative(-retract_mm,config.retract_speed_mm_s))direction=-1;else set_fault(SAFETY_SPI);}
   else if(machine.state==APP_RETRACTING&&direction<0&&motor_target_reached()){direction=0;app_state_dispatch(&machine,EVT_MOVE_DONE);}
   if(machine.state==APP_HOMING&&motor_target_reached()){motor_stop();motor_set_position_mm(config.position_min_mm);direction=0;unload_result="position_min";app_state_dispatch(&machine,EVT_MOVE_DONE);}
   bool spi=true;uint32_t ds=motor_driver_status(&spi);float pos=motor_microsteps_to_mm(&motor_cfg,motor_position_steps());bool manual=machine.state==APP_MANUAL_PUSH||machine.state==APP_MANUAL_PULL;bool unload=machine.state==APP_HOMING;
   if(sg_cal.active&&manual)sg_calibration_add(&sg_cal,ds&0x3ffu);device_config_t safety_cfg=config;if(sg_cal.active)safety_cfg.stallguard_enabled=0;
   safety_fault_t sf=safety_check(&safety,&safety_cfg,false,manual||unload,now,ds,spi,pos,direction);if(sf){if(unload&&(sf==SAFETY_STALL||sf==SAFETY_LIMIT)){motor_stop();motor_set_position_mm(config.position_min_mm);direction=0;unload_result=sf==SAFETY_STALL?"stall":"position_min";safety_init(&safety);app_state_dispatch(&machine,EVT_MOVE_DONE);}else set_fault(sf);}
  }}
  old=b;motor_moving=machine.state==APP_MANUAL_PUSH||machine.state==APP_MANUAL_PULL||machine.state==APP_DOSING||machine.state==APP_RETRACTING||machine.state==APP_HOMING||machine.state==APP_STOPPING;
  if(machine.state==APP_READY&&statistics_dirty())statistics_persist();if(now-last_status>=STATUS_PERIOD_MS){update_telemetry();last_status=now;}
  vTaskDelayUntil(&wake,pdMS_TO_TICKS(CONTROL_PERIOD_MS));
 }
}

static void communication_task(void *arg){
 (void)arg;TickType_t wake=xTaskGetTickCount();
 for(;;){
#if DISPENSER_HAS_RADIO
  machine_command_t command;while(radio_ready&&ble_service_take_command(&command))submit_command(&command);while(radio_ready&&wifi_manager_take_command(&command))submit_command(&command);radio_connected=radio_ready&&(wifi_manager_state()==WIFI_CONNECTED||ble_service_connected());
#else
  radio_connected=false;
#endif
  vTaskDelayUntil(&wake,pdMS_TO_TICKS(10));
 }
}

static void append_text(char **out,size_t *left,const char *text){while(*text&&*left>1){*(*out)++=*text++;(*left)--;}**out=0;}
static void append_uint(char **out,size_t *left,uint32_t value){char digits[10];unsigned n=0;do{digits[n++]=(char)('0'+value%10);value/=10;}while(value&&n<sizeof(digits));while(n&&*left>1){*(*out)++=digits[--n];(*left)--;}**out=0;}
static void append_fixed3(char **out,size_t *left,float value){if(value<0){append_text(out,left,"-");value=-value;}uint32_t scaled=(uint32_t)(value*1000.0f+0.5f);append_uint(out,left,scaled/1000);append_text(out,left,".");uint32_t f=scaled%1000;if(f<100)append_text(out,left,"0");if(f<10)append_text(out,left,"0");append_uint(out,left,f);}
static void format_telemetry(char *json,size_t capacity,const telemetry_snapshot_t *s){char*out=json;size_t left=capacity;*out=0;
 append_text(&out,&left,"{\"state\":\"");append_text(&out,&left,app_state_name(s->state));append_text(&out,&left,"\",\"position_mm\":");append_fixed3(&out,&left,s->position_mm);
 append_text(&out,&left,",\"remaining_course_mm\":");append_fixed3(&out,&left,s->remaining_mm);append_text(&out,&left,",\"used_course_mm\":");append_fixed3(&out,&left,s->used_mm);
 append_text(&out,&left,",\"activation_count\":");append_uint(&out,&left,s->activation_count);append_text(&out,&left,",\"unload_result\":\"");append_text(&out,&left,s->unload_result?s->unload_result:"none");
 append_text(&out,&left,"\",\"sg_result\":");append_uint(&out,&left,s->sg_result);append_text(&out,&left,",\"load\":");append_uint(&out,&left,s->load);append_text(&out,&left,",\"sg_calibrating\":");append_text(&out,&left,s->sg_calibrating?"true":"false");
 append_text(&out,&left,",\"sg_samples\":");append_uint(&out,&left,s->sg_samples);append_text(&out,&left,",\"trigger_dose_mm\":");append_fixed3(&out,&left,s->trigger_dose_mm);append_text(&out,&left,",\"radio_available\":");append_text(&out,&left,DISPENSER_HAS_RADIO?"true":"false");
#if DISPENSER_HAS_RADIO
 append_text(&out,&left,",\"radio_ready\":");append_text(&out,&left,radio_ready?"true":"false");
 append_text(&out,&left,",\"ble_operational\":");append_text(&out,&left,ble_service_operational()?"true":"false");
 append_text(&out,&left,",\"ble_adv_status\":");append_uint(&out,&left,ble_service_advertising_status());
#endif
 append_text(&out,&left,",\"fault\":");append_uint(&out,&left,s->fault);append_text(&out,&left,"}");}

static void telemetry_task(void *arg){
 (void)arg;char json[TELEMETRY_SIZE];telemetry_snapshot_t s;
 for(;;){taskENTER_CRITICAL();s=telemetry_snapshot;taskEXIT_CRITICAL();format_telemetry(json,sizeof(json),&s);puts(json);
#if DISPENSER_HAS_RADIO
  if(radio_ready)wifi_manager_publish(json);
#endif
  vTaskDelay(pdMS_TO_TICKS(STATUS_PERIOD_MS));
 }
}

bool app_tasks_format_query(const char *command,char *response,size_t capacity){
 if(!command||!response||capacity<2)return false;
 if(!strcasecmp(command,"VERSION")){snprintf(response,capacity,DISPENSER_FIRMWARE_NAME " " DISPENSER_FIRMWARE_VERSION "\nOK\n");return true;}
 if(!strcasecmp(command,"STATUS")){telemetry_snapshot_t s;taskENTER_CRITICAL();s=telemetry_snapshot;taskEXIT_CRITICAL();format_telemetry(response,capacity,&s);size_t n=strlen(response);snprintf(response+n,capacity-n,"\nOK\n");return true;}
 if(!strcasecmp(command,"CONFIG")){snprintf(response,capacity,"CONFIG version=%lu screw_pitch_mm=%.3f motor_steps_per_rev=%u microsteps=%u motor_run_current_mA=%u motor_hold_current_mA=%u manual_speed_mm_s=%.3f dosing_speed_mm_s=%.3f trigger_dose_mm=%.3f a1_mm_s2=%.3f amax_mm_s2=%.3f dmax_mm_s2=%.3f d1_mm_s2=%.3f retract_distance_mm=%.3f retract_speed_mm_s=%.3f retract_delay_ms=%lu position_min_mm=%.3f position_max_mm=%.3f manual_timeout_ms=%lu stallguard_threshold=%d stallguard_warning_level=%u stallguard_critical_level=%u stallguard_filter_count=%u stallguard_enabled=%u\nOK\n",
  (unsigned long)config.version,config.screw_pitch_mm,config.motor_steps_per_rev,config.microsteps,config.motor_run_current_mA,config.motor_hold_current_mA,config.manual_speed_mm_s,config.dosing_speed_mm_s,config.trigger_dose_mm,config.a1_mm_s2,config.amax_mm_s2,config.dmax_mm_s2,config.d1_mm_s2,config.retract_distance_mm,config.retract_speed_mm_s,(unsigned long)config.retract_delay_ms,config.position_min_mm,config.position_max_mm,(unsigned long)config.manual_timeout_ms,config.stallguard_threshold,config.stallguard_warning_level,config.stallguard_critical_level,config.stallguard_filter_count,config.stallguard_enabled);return true;}
 if(!strcasecmp(command,"HELP")){snprintf(response,capacity,
  "PasteDispenser " DISPENSER_FIRMWARE_VERSION "\nHELP | VERSION | STATUS | CONFIG\nPUSH | PULL | STOP | DOSE <mm> [mm/s] [retract_mm]\nMOVE <mm> [mm/s] | UNLOAD [mm/s] | ZERO | FAULTRESET | RESET\nSET <parameter> <value> | SGCAL START|FINISH|CANCEL | FLUSH\nOK\n");return true;}
 return false;
}

static void usb_command_task(void *arg){
 (void)arg;char line[512];size_t used=0;
 for(;;){int ch=getchar_timeout_us(0);if(ch==PICO_ERROR_TIMEOUT){vTaskDelay(pdMS_TO_TICKS(5));continue;}if(ch=='\r')continue;if(ch=='\n'){if(used){line[used]=0;
    char response[768];if(app_tasks_format_query(line,response,sizeof(response)))printf("%s",response);
    else{machine_command_t command;bool parsed=line[0]=='{'?command_parse_json(line,used,&command):command_parse_ascii(line,used,&command);if(parsed&&submit_command(&command))puts("OK QUEUED");else puts("ERR SYNTAX_OR_QUEUE");}}
   used=0;}else if(used<sizeof(line)-1)line[used++]=(char)ch;else{used=0;puts("ERR LINE_TOO_LONG");}}
}

static void led_task(void *arg){(void)arg;TickType_t wake=xTaskGetTickCount();for(;;){status_led_update(to_ms_since_boot(get_absolute_time()),radio_connected,motor_moving);vTaskDelayUntil(&wake,pdMS_TO_TICKS(25));}}

void app_tasks_start(void){
 buttons_init();config_store_load(&config);statistics_init();
 motor_cfg=(motor_config_t){.motor_steps_per_rev=config.motor_steps_per_rev,.microsteps=config.microsteps,.motor_run_current_mA=config.motor_run_current_mA,.motor_hold_current_mA=config.motor_hold_current_mA,.screw_pitch_mm=config.screw_pitch_mm,.manual_speed_mm_s=config.manual_speed_mm_s,.a1_mm_s2=config.a1_mm_s2,.amax_mm_s2=config.amax_mm_s2,.dmax_mm_s2=config.dmax_mm_s2,.d1_mm_s2=config.d1_mm_s2};
 app_state_init(&machine);safety_init(&safety);bool motor_ok=motor_init(&motor_cfg);
#if DISPENSER_HAS_RADIO
 status_led_init(false);
#else
 status_led_init(true);
#endif
 if(!motor_ok||!motor_configure_stallguard(config.stallguard_enabled,config.stallguard_threshold))set_fault(SAFETY_SPI);else app_state_dispatch(&machine,EVT_INIT_OK);
 command_queue=xQueueCreate(COMMAND_QUEUE_LENGTH,sizeof(machine_command_t));configASSERT(command_queue);telemetry_snapshot=(telemetry_snapshot_t){.state=machine.state,.unload_result="none",.fault=machine.fault_code};
#if DISPENSER_HAS_RADIO
 configASSERT(xTaskCreate(radio_init_task,"radio",2048,NULL,7,NULL)==pdPASS);
#endif
 configASSERT(xTaskCreate(motor_task,"motor",1024,NULL,6,NULL)==pdPASS);
 configASSERT(xTaskCreate(communication_task,"communications",768,NULL,4,NULL)==pdPASS);
 configASSERT(xTaskCreate(usb_command_task,"usb-command",768,NULL,4,NULL)==pdPASS);
 configASSERT(xTaskCreate(telemetry_task,"telemetry",1024,NULL,2,NULL)==pdPASS);
 configASSERT(xTaskCreate(led_task,"status-led",384,NULL,1,NULL)==pdPASS);
}

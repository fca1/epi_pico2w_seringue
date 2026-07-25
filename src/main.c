#include <stdio.h>
#include "pico/stdlib.h"
#include "app_state.h"
#include "ble_service.h"
#include "board_config.h"
#include "buttons.h"
#include "config_store.h"
#include "motor_control.h"
#include "safety.h"
#include "wifi_manager.h"
#include "hardware/watchdog.h"
#include "hardware/gpio.h"
static device_config_t config;static motor_config_t motor_cfg;static app_machine_t machine;
static safety_t safety;static int direction;static uint32_t phase_ms;static float retract_mm;
static void fault(safety_fault_t f){motor_stop();machine.fault_code=f;app_state_dispatch(&machine,EVT_FAULT);direction=0;}
static bool begin_manual(int d,uint32_t now){if(machine.state!=APP_READY||!motor_manual(d))return false;direction=d;safety_manual_started(&safety,now);app_state_dispatch(&machine,d>0?EVT_PUSH:EVT_PULL);return true;}
static void stop_motion(void){motor_stop();direction=0;if(machine.state!=APP_FAULT)app_state_dispatch(&machine,EVT_STOP);}
static void execute(machine_command_t*c,uint32_t now){
 if(c->kind==CMD_STOP||c->kind==CMD_PUSH_STOP||c->kind==CMD_PULL_STOP){stop_motion();return;}
 if(c->kind==CMD_RESET&&machine.state==APP_FAULT){safety_init(&safety);app_state_dispatch(&machine,EVT_RESET);return;}
 if(machine.state!=APP_READY)return;
 switch(c->kind){case CMD_PUSH_START:begin_manual(1,now);break;case CMD_PULL_START:begin_manual(-1,now);break;
 case CMD_SET_ZERO:motor_set_zero();break;case CMD_MOVE_RELATIVE:if(motor_move_relative(c->distance_mm,c->speed_mm_s>0?c->speed_mm_s:config.dosing_speed_mm_s)){direction=c->distance_mm>=0?1:-1;retract_mm=0;app_state_dispatch(&machine,EVT_DOSE);}break;
 case CMD_DOSE:if(c->distance_mm>0&&motor_move_relative(c->distance_mm,c->speed_mm_s>0?c->speed_mm_s:config.dosing_speed_mm_s)){direction=1;retract_mm=c->retract_mm>=0?c->retract_mm:config.retract_distance_mm;app_state_dispatch(&machine,EVT_DOSE);}break;default:break;}}
int main(void){stdio_init_all();buttons_init();config_store_load(&config);
 motor_cfg=(motor_config_t){config.motor_steps_per_rev,config.microsteps,config.screw_pitch_mm,config.manual_speed_mm_s,config.acceleration_mm_s2};
 app_state_init(&machine);safety_init(&safety);bool motor_ok=motor_init(&motor_cfg),ble_ok=ble_service_init();
 bool provision_at_boot=!gpio_get(PIN_SW_PULL);if(!motor_ok||!motor_configure_stallguard(config.stallguard_enabled,config.stallguard_threshold))fault(SAFETY_SPI);else app_state_dispatch(&machine,EVT_INIT_OK);if(ble_ok){wifi_manager_init(&config);if(provision_at_boot)wifi_manager_open_provisioning(300000);}
 button_state_t old={0};uint32_t last=0,conflict_since=0;char json[180];
 while(true){uint32_t now=to_ms_since_boot(get_absolute_time());button_state_t b=buttons_update(now);machine_command_t command;
  if(b.conflict){if(!conflict_since)conflict_since=now;fault(SAFETY_BUTTON_CONFLICT);if(now-conflict_since>=5000){motor_stop();config_store_factory_reset();watchdog_reboot(0,0,0);}}else{conflict_since=0;if(machine.state!=APP_FAULT){
   if(b.push&&!old.push)begin_manual(1,now);else if(b.pull&&!old.pull)begin_manual(-1,now);
   else if(!b.push&&!b.pull&&(machine.state==APP_MANUAL_PUSH||machine.state==APP_MANUAL_PULL))stop_motion();
   if(ble_ok&&ble_service_take_command(&command))execute(&command,now);
   if(ble_ok&&wifi_manager_take_command(&command))execute(&command,now);
   if(machine.state==APP_STOPPING&&motor_is_stopped())app_state_dispatch(&machine,EVT_STOPPED);
   if(machine.state==APP_DOSING&&motor_target_reached()){direction=0;if(retract_mm>0){app_state_dispatch(&machine,EVT_MOVE_DONE);phase_ms=now;}else{app_state_dispatch(&machine,EVT_MOVE_DONE);app_state_dispatch(&machine,EVT_MOVE_DONE);}}
   if(machine.state==APP_RETRACTING&&direction==0&&now-phase_ms>=config.retract_delay_ms){if(motor_move_relative(-retract_mm,config.retract_speed_mm_s))direction=-1;else fault(SAFETY_SPI);}
   else if(machine.state==APP_RETRACTING&&direction<0&&motor_target_reached()){direction=0;app_state_dispatch(&machine,EVT_MOVE_DONE);}
   bool spi=true;uint32_t ds=motor_driver_status(&spi);float pos=motor_microsteps_to_mm(&motor_cfg,motor_position_steps());bool manual=machine.state==APP_MANUAL_PUSH||machine.state==APP_MANUAL_PULL;
   safety_fault_t sf=safety_check(&safety,&config,false,manual,now,ds,spi,pos,direction);if(sf)fault(sf);
  }} old=b;
  if(now-last>=STATUS_PERIOD_MS){float pos=motor_microsteps_to_mm(&motor_cfg,motor_position_steps());snprintf(json,sizeof(json),"{\"state\":\"%s\",\"position_mm\":%.3f,\"sg_result\":%u,\"fault\":%lu,\"wifi\":\"%s\"}",app_state_name(machine.state),(double)pos,safety.sg_filtered,(unsigned long)machine.fault_code,wifi_manager_state_name());puts(json);if(ble_ok){ble_service_publish(json);wifi_manager_publish(json);}last=now;}
  sleep_ms(CONTROL_PERIOD_MS);
 }}

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "app_state.h"
#include "motor_control.h"
#include "command_api.h"
#include "safety.h"
#include "stallguard_calibration.h"
#include "ws_crypto.h"
#include "tmc_current.h"
int main(void){
 motor_config_t c={.motor_steps_per_rev=200,.microsteps=16,.motor_run_current_mA=800,.motor_hold_current_mA=300,.screw_pitch_mm=2.0f,.manual_speed_mm_s=5.0f,.a1_mm_s2=100.0f,.amax_mm_s2=100.0f,.dmax_mm_s2=100.0f,.d1_mm_s2=100.0f};
 assert(fabsf(motor_microsteps_per_mm(&c)-1600.0f)<.001f);
 assert(motor_mm_to_microsteps(&c,.8f)==1280);assert(motor_mm_to_microsteps(&c,-.1f)==-160);
 assert(motor_acceleration_to_tmc(&c,100.0f)==2443u);
 app_machine_t m;app_state_init(&m);assert(app_state_dispatch(&m,EVT_INIT_OK)&&m.state==APP_READY);
 assert(app_state_dispatch(&m,EVT_PUSH)&&m.state==APP_MANUAL_PUSH);assert(!app_state_dispatch(&m,EVT_PULL));
 assert(app_state_dispatch(&m,EVT_RELEASE)&&m.state==APP_STOPPING);assert(app_state_dispatch(&m,EVT_STOPPED)&&m.state==APP_READY);
 assert(app_state_dispatch(&m,EVT_DOSE)&&m.state==APP_DOSING);assert(app_state_dispatch(&m,EVT_FAULT)&&m.state==APP_FAULT);
 assert(app_state_dispatch(&m,EVT_RESET)&&m.state==APP_READY);
 assert(app_state_dispatch(&m,EVT_HOME)&&m.state==APP_HOMING);assert(app_state_dispatch(&m,EVT_MOVE_DONE)&&m.state==APP_READY);
 const char *json="{\"command\":\"dose\",\"distance_mm\":0.8,\"speed_mm_s\":5}";machine_command_t cmd;assert(command_parse_json(json,strlen(json),&cmd));assert(cmd.kind==CMD_DOSE&&fabsf(cmd.distance_mm-.8f)<.001f);
 const char *trigger="{\"command\":\"set_trigger_dose\",\"distance_mm\":1.25}";assert(command_parse_json(trigger,strlen(trigger),&cmd));assert(cmd.kind==CMD_SET_TRIGGER_DOSE&&fabsf(cmd.distance_mm-1.25f)<.001f);
 const char *flush="{\"command\":\"flush_statistics\"}";assert(command_parse_json(flush,strlen(flush),&cmd));assert(cmd.kind==CMD_FLUSH_STATISTICS);
 const char *unload="{\"command\":\"unload_syringe\",\"speed_mm_s\":3}";assert(command_parse_json(unload,strlen(unload),&cmd));assert(cmd.kind==CMD_UNLOAD_SYRINGE&&fabsf(cmd.speed_mm_s-3.0f)<.001f);
 const char *setcfg="{\"command\":\"set_config\",\"parameter\":\"dosing_speed_mm_s\",\"value\":4}";assert(command_parse_json(setcfg,strlen(setcfg),&cmd));assert(cmd.kind==CMD_SET_CONFIG&&cmd.parameter==CFG_DOSING_SPEED_MM_S&&fabsf(cmd.value-4.0f)<.001f);
 const char *ascii_dose="DOSE 0.8 4 0.1";assert(command_parse_ascii(ascii_dose,strlen(ascii_dose),&cmd));assert(cmd.kind==CMD_DOSE&&fabsf(cmd.distance_mm-.8f)<.001f&&fabsf(cmd.speed_mm_s-4)<.001f&&fabsf(cmd.retract_mm-.1f)<.001f);
 const char *ascii_set="set motor_run_current_mA 900";assert(command_parse_ascii(ascii_set,strlen(ascii_set),&cmd));assert(cmd.kind==CMD_SET_CONFIG&&cmd.parameter==CFG_RUN_CURRENT_MA&&fabsf(cmd.value-900)<.001f);
 const char *ascii_amax="SET AMAX_MM_S2 250";assert(command_parse_ascii(ascii_amax,strlen(ascii_amax),&cmd));assert(cmd.kind==CMD_SET_CONFIG&&cmd.parameter==CFG_AMAX_MM_S2&&fabsf(cmd.value-250)<.001f);
 const char *ascii_reset="RESET";assert(command_parse_ascii(ascii_reset,strlen(ascii_reset),&cmd)&&cmd.kind==CMD_REBOOT);
 device_config_t d={.manual_timeout_ms=1000,.position_min_mm=0,.position_max_mm=10,.stallguard_filter_count=3,.stallguard_warning_level=100};safety_t s;safety_init(&s);safety_manual_started(&s,10);assert(safety_check(&s,&d,false,true,500,200,true,5,1)==SAFETY_OK);assert(safety_check(&s,&d,false,true,1011,200,true,5,1)==SAFETY_TIMEOUT);
 sg_calibration_t cal;uint16_t baseline,warning,critical;sg_calibration_start(&cal);for(int i=0;i<120;i++)sg_calibration_add(&cal,400+(i%5));assert(sg_calibration_finish(&cal,&baseline,&warning,&critical));assert(baseline==402&&warning==281&&critical==201);
 char accept[29];assert(ws_crypto_accept("dGhlIHNhbXBsZSBub25jZQ==",24,accept));assert(!strcmp(accept,"s3pPLMBiTxaQ9kYGzzhZRbK+xOo="));
 assert(tmc_current_scale_from_mA(800,0.1f)==12);assert(tmc_current_mA_from_scale(12,0.1f)==778);assert(tmc_current_scale_from_mA(300,0.1f)==4);assert(tmc_current_mA_from_scale(4,0.1f)==299);
 puts("All tests passed");
}

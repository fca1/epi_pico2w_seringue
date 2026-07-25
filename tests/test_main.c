#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "app_state.h"
#include "motor_control.h"
#include "command_api.h"
#include "safety.h"
int main(void){
 motor_config_t c={200,16,2.0f,5.0f,100.0f};
 assert(fabsf(motor_microsteps_per_mm(&c)-1600.0f)<.001f);
 assert(motor_mm_to_microsteps(&c,.8f)==1280);assert(motor_mm_to_microsteps(&c,-.1f)==-160);
 app_machine_t m;app_state_init(&m);assert(app_state_dispatch(&m,EVT_INIT_OK)&&m.state==APP_READY);
 assert(app_state_dispatch(&m,EVT_PUSH)&&m.state==APP_MANUAL_PUSH);assert(!app_state_dispatch(&m,EVT_PULL));
 assert(app_state_dispatch(&m,EVT_RELEASE)&&m.state==APP_STOPPING);assert(app_state_dispatch(&m,EVT_STOPPED)&&m.state==APP_READY);
 assert(app_state_dispatch(&m,EVT_DOSE)&&m.state==APP_DOSING);assert(app_state_dispatch(&m,EVT_FAULT)&&m.state==APP_FAULT);
 assert(app_state_dispatch(&m,EVT_RESET)&&m.state==APP_READY);
 const char *json="{\"command\":\"dose\",\"distance_mm\":0.8,\"speed_mm_s\":5}";machine_command_t cmd;assert(command_parse_json(json,strlen(json),&cmd));assert(cmd.kind==CMD_DOSE&&fabsf(cmd.distance_mm-.8f)<.001f);
 device_config_t d={.manual_timeout_ms=1000,.position_min_mm=0,.position_max_mm=10,.stallguard_filter_count=3,.stallguard_warning_level=100};safety_t s;safety_init(&s);safety_manual_started(&s,10);assert(safety_check(&s,&d,false,true,500,200,true,5,1)==SAFETY_OK);assert(safety_check(&s,&d,false,true,1011,200,true,5,1)==SAFETY_TIMEOUT);
 puts("All tests passed");
}

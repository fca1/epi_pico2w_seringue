#include "device_config.h"
#include "board_config.h"
#include <math.h>
#include <string.h>

void device_config_defaults(device_config_t *c){
 memset(c,0,sizeof(*c));
 c->version=DEVICE_CONFIG_VERSION;c->screw_pitch_mm=2;c->motor_steps_per_rev=200;c->microsteps=16;
 c->motor_run_current_mA=800;c->motor_hold_current_mA=300;c->manual_speed_mm_s=5;c->dosing_speed_mm_s=5;
 c->trigger_dose_mm=.8f;c->a1_mm_s2=100;c->amax_mm_s2=100;c->dmax_mm_s2=100;c->d1_mm_s2=100;
 c->retract_distance_mm=.1f;c->retract_speed_mm_s=3;c->retract_delay_ms=50;
 c->position_min_mm=0;c->position_max_mm=120;c->manual_timeout_ms=30000;
 c->stallguard_threshold=0;c->stallguard_filter_count=4;
}

bool device_config_validate(const device_config_t *c){
 if(!c||c->version!=DEVICE_CONFIG_VERSION||!c->motor_steps_per_rev||!isfinite(c->screw_pitch_mm)||c->screw_pitch_mm<=0||c->microsteps>256||!c->microsteps||(c->microsteps&(c->microsteps-1)))return false;
 if(c->stallguard_threshold<-64||c->stallguard_threshold>63)return false;
 if(c->stallguard_enabled&&(!c->stallguard_baseline||!c->stallguard_filter_count||c->stallguard_critical_level>=c->stallguard_warning_level||c->stallguard_warning_level>=c->stallguard_baseline))return false;
 return c->motor_run_current_mA>=TMC_INTERNAL_CURRENT_MIN_MA&&c->motor_run_current_mA<=TMC_INTERNAL_CURRENT_MAX_MA&&c->motor_hold_current_mA>=TMC_INTERNAL_CURRENT_MIN_MA&&c->motor_hold_current_mA<=c->motor_run_current_mA&&
  isfinite(c->manual_speed_mm_s)&&c->manual_speed_mm_s>0&&c->manual_speed_mm_s<=MAX_SPEED_MM_S&&isfinite(c->dosing_speed_mm_s)&&c->dosing_speed_mm_s>0&&c->dosing_speed_mm_s<=MAX_SPEED_MM_S&&
  isfinite(c->trigger_dose_mm)&&c->trigger_dose_mm>0&&c->trigger_dose_mm<=100&&isfinite(c->a1_mm_s2)&&c->a1_mm_s2>0&&c->a1_mm_s2<=MAX_ACCEL_MM_S2&&
  isfinite(c->amax_mm_s2)&&c->amax_mm_s2>0&&c->amax_mm_s2<=MAX_ACCEL_MM_S2&&isfinite(c->dmax_mm_s2)&&c->dmax_mm_s2>0&&c->dmax_mm_s2<=MAX_ACCEL_MM_S2&&
  isfinite(c->d1_mm_s2)&&c->d1_mm_s2>0&&c->d1_mm_s2<=MAX_ACCEL_MM_S2&&isfinite(c->retract_distance_mm)&&c->retract_distance_mm>=0&&c->retract_distance_mm<=100&&
  isfinite(c->retract_speed_mm_s)&&c->retract_speed_mm_s>0&&c->retract_speed_mm_s<=MAX_SPEED_MM_S&&isfinite(c->position_min_mm)&&isfinite(c->position_max_mm)&&
  c->position_max_mm>c->position_min_mm&&c->manual_timeout_ms>=100;
}

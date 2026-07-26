#include "motor_control.h"
#ifndef UNIT_TEST
#include "tmc5130.h"
#endif
#include <math.h>
#ifndef UNIT_TEST
static motor_config_t cfg;
#endif
float motor_microsteps_per_mm(const motor_config_t*c){return(float)c->motor_steps_per_rev*c->microsteps/c->screw_pitch_mm;}
int32_t motor_mm_to_microsteps(const motor_config_t*c,float mm){float n=mm*motor_microsteps_per_mm(c);return(int32_t)(n>=0?n+.5f:n-.5f);}
float motor_microsteps_to_mm(const motor_config_t*c,int32_t s){return s/motor_microsteps_per_mm(c);}
uint32_t motor_acceleration_to_tmc(const motor_config_t*c,float mm_s2){if(!c||mm_s2<=0)return 0;double steps_s2=(double)mm_s2*motor_microsteps_per_mm(c);double reg=steps_s2*2199023255552.0/(12000000.0*12000000.0);return reg>=4294967295.0?UINT32_MAX:(uint32_t)(reg+0.5);}
bool motor_config_valid(const motor_config_t*c){if(!c||!c->motor_steps_per_rev||!c->motor_run_current_mA||!c->motor_hold_current_mA||c->motor_hold_current_mA>c->motor_run_current_mA||c->screw_pitch_mm<=0||c->manual_speed_mm_s<=0||c->a1_mm_s2<=0||c->amax_mm_s2<=0||c->dmax_mm_s2<=0||c->d1_mm_s2<=0)return false;uint16_t m=c->microsteps;if(m>256||!m||(m&(m-1)))return false;return motor_acceleration_to_tmc(c,c->a1_mm_s2)<=65535&&motor_acceleration_to_tmc(c,c->amax_mm_s2)<=65535&&motor_acceleration_to_tmc(c,c->dmax_mm_s2)<=65535&&motor_acceleration_to_tmc(c,c->d1_mm_s2)<=65535;}
#ifndef UNIT_TEST
static uint32_t vel(float mm_s){return(uint32_t)(fabsf(mm_s)*motor_microsteps_per_mm(&cfg)*16777216.0f/12000000.0f);}
bool motor_init(const motor_config_t*c){if(!motor_config_valid(c))return false;cfg=*c;if(!tmc5130_init()||!tmc5130_configure(c->microsteps,c->motor_run_current_mA,c->motor_hold_current_mA))return false;tmc5130_enable(true);return tmc5130_set_ramp(vel(c->manual_speed_mm_s),motor_acceleration_to_tmc(c,c->a1_mm_s2),motor_acceleration_to_tmc(c,c->amax_mm_s2),motor_acceleration_to_tmc(c,c->dmax_mm_s2),motor_acceleration_to_tmc(c,c->d1_mm_s2));}
bool motor_manual(int d){return tmc5130_velocity(d,vel(cfg.manual_speed_mm_s));}
bool motor_move_relative(float mm,float speed){tmc_reply_t p=tmc5130_read(TMC_XACTUAL);return p.ok&&tmc5130_write(TMC_VMAX,vel(speed))&&tmc5130_position((int32_t)p.value+motor_mm_to_microsteps(&cfg,mm));}
bool motor_stop(void){return tmc5130_stop();}bool motor_is_stopped(void){return tmc5130_read(TMC_VACTUAL).value==0;}
int32_t motor_position_steps(void){return(int32_t)tmc5130_read(TMC_XACTUAL).value;}
bool motor_target_reached(void){tmc_reply_t a=tmc5130_read(TMC_XACTUAL),t=tmc5130_read(TMC_XTARGET);return a.ok&&t.ok&&a.value==t.value&&motor_is_stopped();}
uint32_t motor_driver_status(bool*ok){tmc_reply_t r=tmc5130_read(TMC_DRV_STATUS);if(ok)*ok=r.ok;return r.value;}
bool motor_set_zero(void){return tmc5130_write(TMC_XACTUAL,0)&&tmc5130_write(TMC_XTARGET,0);}
bool motor_set_position_mm(float mm){int32_t p=motor_mm_to_microsteps(&cfg,mm);return tmc5130_write(TMC_XACTUAL,(uint32_t)p)&&tmc5130_write(TMC_XTARGET,(uint32_t)p);}
bool motor_configure_stallguard(bool enabled,int8_t threshold){return tmc5130_configure_stallguard(enabled,threshold);}
#endif

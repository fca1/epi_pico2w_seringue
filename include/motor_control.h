#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct {uint16_t motor_steps_per_rev,microsteps;float screw_pitch_mm,manual_speed_mm_s,acceleration_mm_s2;} motor_config_t;
float motor_microsteps_per_mm(const motor_config_t*);int32_t motor_mm_to_microsteps(const motor_config_t*,float);
float motor_microsteps_to_mm(const motor_config_t*,int32_t);bool motor_config_valid(const motor_config_t*);
bool motor_init(const motor_config_t*);bool motor_manual(int);bool motor_move_relative(float,float);
bool motor_stop(void);bool motor_is_stopped(void);int32_t motor_position_steps(void);
bool motor_target_reached(void);uint32_t motor_driver_status(bool *ok);bool motor_set_zero(void);
bool motor_configure_stallguard(bool enabled,int8_t threshold);

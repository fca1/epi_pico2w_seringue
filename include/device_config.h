#pragma once
#include <stdbool.h>
#include <stdint.h>
#define DEVICE_CONFIG_VERSION 1u
typedef struct {
 uint32_t version,sequence; char wifi_ssid[33],wifi_password[65];
 float screw_pitch_mm; uint16_t motor_steps_per_rev,microsteps;
 float manual_speed_mm_s,dosing_speed_mm_s,acceleration_mm_s2;
 float retract_distance_mm,retract_speed_mm_s; uint32_t retract_delay_ms;
 float position_min_mm,position_max_mm; uint32_t manual_timeout_ms;
 int8_t stallguard_threshold; uint16_t stallguard_warning_level,stallguard_filter_count;
 uint8_t stallguard_enabled; uint32_t crc;
} device_config_t;
void device_config_defaults(device_config_t *cfg);
bool device_config_validate(const device_config_t *cfg);

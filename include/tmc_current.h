#pragma once
#include <stdint.h>
uint16_t tmc_internal_full_scale_mA(float reference_resistor_ohm);
uint8_t tmc_internal_current_scale_from_mA(uint16_t current_mA,float reference_resistor_ohm);
uint16_t tmc_internal_current_mA_from_scale(uint8_t scale,float reference_resistor_ohm);

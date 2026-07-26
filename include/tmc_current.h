#pragma once
#include <stdint.h>
uint8_t tmc_current_scale_from_mA(uint16_t current_mA,float sense_resistor_ohm);
uint16_t tmc_current_mA_from_scale(uint8_t scale,float sense_resistor_ohm);

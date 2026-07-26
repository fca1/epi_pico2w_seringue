#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct {bool active;uint32_t samples;uint32_t sum;uint16_t minimum,maximum;} sg_calibration_t;
void sg_calibration_start(sg_calibration_t *cal);
void sg_calibration_add(sg_calibration_t *cal,uint16_t sg_result);
bool sg_calibration_finish(sg_calibration_t *cal,uint16_t *baseline,uint16_t *warning,uint16_t *critical);

#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "device_config.h"
typedef enum { SAFETY_OK,SAFETY_BUTTON_CONFLICT,SAFETY_SPI,SAFETY_OVERTEMP,SAFETY_STALL,SAFETY_TIMEOUT,SAFETY_LIMIT } safety_fault_t;
typedef struct {safety_fault_t fault;uint16_t sg_filtered;uint16_t low_count;uint32_t manual_started_ms;} safety_t;
void safety_init(safety_t *s); void safety_manual_started(safety_t*s,uint32_t now);
safety_fault_t safety_check(safety_t*s,const device_config_t*c,bool conflict,bool manual,uint32_t now,uint32_t drv_status,bool spi_ok,float position_mm,int direction);

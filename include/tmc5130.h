#pragma once
#include <stdbool.h>
#include <stdint.h>
enum { TMC_GCONF=0x00,TMC_GSTAT=0x01,TMC_IOIN=0x04,TMC_IHOLD_IRUN=0x10,TMC_TCOOLTHRS=0x14,TMC_RAMPMODE=0x20,
 TMC_XACTUAL=0x21,TMC_VACTUAL=0x22,TMC_VSTART=0x23,TMC_A1=0x24,TMC_V1=0x25,
 TMC_AMAX=0x26,TMC_VMAX=0x27,TMC_DMAX=0x28,TMC_D1=0x2A,TMC_VSTOP=0x2B,
 TMC_XTARGET=0x2D,TMC_CHOPCONF=0x6C,TMC_COOLCONF=0x6D,TMC_DRV_STATUS=0x6F };
typedef struct {uint8_t status;uint32_t value;bool ok;} tmc_reply_t;
bool tmc5130_init(void); tmc_reply_t tmc5130_read(uint8_t);
bool tmc5130_write(uint8_t,uint32_t); void tmc5130_enable(bool);
bool tmc5130_configure(uint16_t,uint16_t,uint16_t); bool tmc5130_set_ramp(uint32_t,uint32_t,uint32_t);
bool tmc5130_velocity(int,uint32_t); bool tmc5130_position(int32_t); bool tmc5130_stop(void);
bool tmc5130_configure_stallguard(bool enabled,int8_t threshold);

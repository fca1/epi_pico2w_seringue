#include "config_store.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/flash.h"
#include "storage_layout.h"
#include <stddef.h>
#include <string.h>

#define SLOT_SIZE FLASH_SECTOR_SIZE
#define STORE_OFFSET (DISPENSER_BTSTACK_OFFSET-(2u*SLOT_SIZE))

/* Version 5 included two obsolete radio credential buffers. They are kept
   only as anonymous padding here so existing mechanical settings migrate. */
typedef struct {
 uint32_t version,sequence;uint8_t obsolete_radio_data[98];
 float screw_pitch_mm;uint16_t motor_steps_per_rev,microsteps;
 uint16_t motor_run_current_mA,motor_hold_current_mA;
 float manual_speed_mm_s,dosing_speed_mm_s,trigger_dose_mm;
 float a1_mm_s2,amax_mm_s2,dmax_mm_s2,d1_mm_s2;
 float retract_distance_mm,retract_speed_mm_s;uint32_t retract_delay_ms;
 float position_min_mm,position_max_mm;uint32_t manual_timeout_ms;
 int8_t stallguard_threshold;uint16_t stallguard_warning_level,stallguard_filter_count;
 uint16_t stallguard_critical_level,stallguard_baseline;float stallguard_calibration_speed_mm_s;
 uint8_t stallguard_enabled;uint32_t crc;
} config_v5_legacy_t;

static uint32_t crc32(const void *data,size_t len){uint32_t c=0xffffffffu;const uint8_t*p=data;while(len--){c^=*p++;for(int i=0;i<8;i++)c=(c>>1)^(0xedb88320u&(0u-(c&1u)));}return~c;}
static bool valid(const device_config_t*c){return device_config_validate(c)&&c->crc==crc32(c,offsetof(device_config_t,crc));}
static bool valid_v5(const config_v5_legacy_t*c){return c->version==5&&c->crc==crc32(c,offsetof(config_v5_legacy_t,crc));}

static void migrate_v5(device_config_t*out,const config_v5_legacy_t*old){device_config_defaults(out);out->sequence=old->sequence;
 out->screw_pitch_mm=old->screw_pitch_mm;out->motor_steps_per_rev=old->motor_steps_per_rev;out->microsteps=old->microsteps;out->motor_run_current_mA=old->motor_run_current_mA;out->motor_hold_current_mA=old->motor_hold_current_mA;
 out->manual_speed_mm_s=old->manual_speed_mm_s;out->dosing_speed_mm_s=old->dosing_speed_mm_s;out->trigger_dose_mm=old->trigger_dose_mm;out->a1_mm_s2=old->a1_mm_s2;out->amax_mm_s2=old->amax_mm_s2;out->dmax_mm_s2=old->dmax_mm_s2;out->d1_mm_s2=old->d1_mm_s2;
 out->retract_distance_mm=old->retract_distance_mm;out->retract_speed_mm_s=old->retract_speed_mm_s;out->retract_delay_ms=old->retract_delay_ms;out->position_min_mm=old->position_min_mm;out->position_max_mm=old->position_max_mm;out->manual_timeout_ms=old->manual_timeout_ms;
 out->stallguard_threshold=old->stallguard_threshold;out->stallguard_warning_level=old->stallguard_warning_level;out->stallguard_filter_count=old->stallguard_filter_count;out->stallguard_critical_level=old->stallguard_critical_level;out->stallguard_baseline=old->stallguard_baseline;out->stallguard_calibration_speed_mm_s=old->stallguard_calibration_speed_mm_s;out->stallguard_enabled=old->stallguard_enabled;
}

bool config_store_load(device_config_t*out){const void*pa=(const void*)(XIP_BASE+STORE_OFFSET),*pb=(const void*)(XIP_BASE+STORE_OFFSET+SLOT_SIZE);const device_config_t*a=pa,*b=pb;bool va=valid(a),vb=valid(b);if(va||vb){*out=vb&&(!va||b->sequence>a->sequence)?*b:*a;return true;}const config_v5_legacy_t*a5=pa,*b5=pb;bool va5=valid_v5(a5),vb5=valid_v5(b5);if(va5||vb5){migrate_v5(out,vb5&&(!va5||b5->sequence>a5->sequence)?b5:a5);return true;}device_config_defaults(out);return false;}
typedef struct{uint32_t offset;uint8_t page[FLASH_PAGE_SIZE];} write_ctx_t;
static void do_write(void*p){write_ctx_t*w=p;flash_range_erase(w->offset,SLOT_SIZE);flash_range_program(w->offset,w->page,FLASH_PAGE_SIZE);}
bool config_store_save(device_config_t*c){if(!device_config_validate(c)||sizeof(*c)>FLASH_PAGE_SIZE)return false;device_config_t old;config_store_load(&old);c->sequence=old.sequence+1;c->crc=crc32(c,offsetof(device_config_t,crc));write_ctx_t w={STORE_OFFSET+(c->sequence&1u)*SLOT_SIZE,{0xff}};memcpy(w.page,c,sizeof(*c));return flash_safe_execute(do_write,&w,1000)==PICO_OK;}
static void do_erase(void*p){(void)p;flash_range_erase(STORE_OFFSET,2u*SLOT_SIZE);}
bool config_store_factory_reset(void){return flash_safe_execute(do_erase,0,1000)==PICO_OK;}

#include "config_store.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/flash.h"
#include "pico/btstack_flash_bank.h"
#include <stddef.h>
#include <string.h>
#define SLOT_SIZE FLASH_SECTOR_SIZE
#define STORE_OFFSET (PICO_FLASH_BANK_STORAGE_OFFSET-(2u*SLOT_SIZE))
static uint32_t crc32(const void *data,size_t len){uint32_t c=0xffffffffu;const uint8_t*p=data;while(len--){c^=*p++;for(int i=0;i<8;i++)c=(c>>1)^(0xedb88320u&-(int32_t)(c&1));}return~c;}
void device_config_defaults(device_config_t*c){memset(c,0,sizeof(*c));c->version=DEVICE_CONFIG_VERSION;c->screw_pitch_mm=2;c->motor_steps_per_rev=200;c->microsteps=16;c->manual_speed_mm_s=5;c->dosing_speed_mm_s=5;c->acceleration_mm_s2=100;c->retract_distance_mm=.1f;c->retract_speed_mm_s=3;c->retract_delay_ms=50;c->position_min_mm=0;c->position_max_mm=120;c->manual_timeout_ms=30000;c->stallguard_threshold=0;c->stallguard_warning_level=100;c->stallguard_filter_count=4;}
bool device_config_validate(const device_config_t*c){if(!c||c->version!=DEVICE_CONFIG_VERSION||!c->motor_steps_per_rev||c->screw_pitch_mm<=0||c->microsteps>256||!c->microsteps||(c->microsteps&(c->microsteps-1)))return false;return c->manual_speed_mm_s>0&&c->manual_speed_mm_s<=25&&c->dosing_speed_mm_s>0&&c->acceleration_mm_s2>0&&c->position_max_mm>c->position_min_mm&&c->manual_timeout_ms>=100;}
static bool valid(const device_config_t*c){return device_config_validate(c)&&c->crc==crc32(c,offsetof(device_config_t,crc));}
bool config_store_load(device_config_t*out){const device_config_t*a=(const void*)(XIP_BASE+STORE_OFFSET),*b=(const void*)(XIP_BASE+STORE_OFFSET+SLOT_SIZE);bool va=valid(a),vb=valid(b);if(!va&&!vb){device_config_defaults(out);return false;}*out=vb&&(!va||b->sequence>a->sequence)?*b:*a;return true;}
typedef struct{uint32_t offset;uint8_t page[FLASH_PAGE_SIZE];} write_ctx_t;
static void do_write(void*p){write_ctx_t*w=p;flash_range_erase(w->offset,SLOT_SIZE);flash_range_program(w->offset,w->page,FLASH_PAGE_SIZE);}
bool config_store_save(device_config_t*c){if(!device_config_validate(c)||sizeof(*c)>FLASH_PAGE_SIZE)return false;device_config_t old;config_store_load(&old);c->sequence=old.sequence+1;c->crc=crc32(c,offsetof(device_config_t,crc));write_ctx_t w={STORE_OFFSET+(c->sequence&1u)*SLOT_SIZE,{0xff}};memcpy(w.page,c,sizeof(*c));return flash_safe_execute(do_write,&w,1000)==PICO_OK;}
static void do_erase(void*p){(void)p;flash_range_erase(STORE_OFFSET,2u*SLOT_SIZE);}
bool config_store_factory_reset(void){return flash_safe_execute(do_erase,0,1000)==PICO_OK;}

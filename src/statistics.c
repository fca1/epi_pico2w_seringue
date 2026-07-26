#include "statistics.h"
#include "hardware/flash.h"
#include "pico/flash.h"
#include "storage_layout.h"
#include <stddef.h>
#include <string.h>
#define STATS_MAGIC 0x53544154u
#define STATS_SECTOR_SIZE FLASH_SECTOR_SIZE
#define STATS_OFFSET (DISPENSER_BTSTACK_OFFSET-(4u*STATS_SECTOR_SIZE))
#define PAGES_PER_SECTOR (STATS_SECTOR_SIZE/FLASH_PAGE_SIZE)
typedef struct{uint32_t magic,sequence,count,crc;} record_t;
static uint32_t count,sequence,current_sector,current_page;static bool dirty;
static uint32_t crc32(const void*d,size_t n){uint32_t c=0xffffffffu;const uint8_t*p=d;while(n--){c^=*p++;for(int i=0;i<8;i++)c=(c>>1)^(0xedb88320u&-(int32_t)(c&1));}return~c;}
static bool valid(const record_t*r){return r->magic==STATS_MAGIC&&r->crc==crc32(r,offsetof(record_t,crc));}
void statistics_init(void){count=sequence=current_sector=current_page=0;dirty=false;bool found=false;for(uint32_t s=0;s<2;s++)for(uint32_t p=0;p<PAGES_PER_SECTOR;p++){const record_t*r=(const void*)(XIP_BASE+STATS_OFFSET+s*STATS_SECTOR_SIZE+p*FLASH_PAGE_SIZE);if(valid(r)&&(!found||r->sequence>sequence)){found=true;sequence=r->sequence;count=r->count;current_sector=s;current_page=p;}}}
void statistics_increment(void){count++;dirty=true;}void statistics_flush(void){count=0;dirty=true;}bool statistics_dirty(void){return dirty;}uint32_t statistics_activation_count(void){return count;}
typedef struct{uint32_t offset;bool erase;uint8_t page[FLASH_PAGE_SIZE];}write_t;
static void do_write(void*arg){write_t*w=arg;if(w->erase)flash_range_erase(w->offset-(w->offset%STATS_SECTOR_SIZE),STATS_SECTOR_SIZE);flash_range_program(w->offset,w->page,FLASH_PAGE_SIZE);}
bool statistics_persist(void){if(!dirty)return true;uint32_t next_page=current_page+1,next_sector=current_sector;bool erase=false;if(next_page>=PAGES_PER_SECTOR){next_sector^=1u;next_page=0;erase=true;}record_t r={STATS_MAGIC,sequence+1,count,0};r.crc=crc32(&r,offsetof(record_t,crc));write_t w;memset(&w,0xff,sizeof(w));w.offset=STATS_OFFSET+next_sector*STATS_SECTOR_SIZE+next_page*FLASH_PAGE_SIZE;w.erase=erase;memcpy(w.page,&r,sizeof(r));if(flash_safe_execute(do_write,&w,1000)!=PICO_OK)return false;sequence=r.sequence;current_sector=next_sector;current_page=next_page;dirty=false;return true;}
static void do_erase(void*arg){(void)arg;flash_range_erase(STATS_OFFSET,2u*STATS_SECTOR_SIZE);}
bool statistics_factory_reset(void){bool ok=flash_safe_execute(do_erase,NULL,1000)==PICO_OK;if(ok){count=sequence=current_sector=current_page=0;dirty=false;}return ok;}

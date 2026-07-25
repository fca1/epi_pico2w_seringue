#include "safety.h"
void safety_init(safety_t*s){*s=(safety_t){0};}
void safety_manual_started(safety_t*s,uint32_t now){s->manual_started_ms=now;}
safety_fault_t safety_check(safety_t*s,const device_config_t*c,bool both,bool manual,uint32_t now,uint32_t ds,bool spi,float pos,int dir){
 if(s->fault)return s->fault;
 if(both)return s->fault=SAFETY_BUTTON_CONFLICT;
 if(!spi)return s->fault=SAFETY_SPI;
 if(ds&(1u<<25))return s->fault=SAFETY_OVERTEMP;
 if(manual&&now-s->manual_started_ms>c->manual_timeout_ms)return s->fault=SAFETY_TIMEOUT;
 if((dir<0&&pos<=c->position_min_mm)||(dir>0&&pos>=c->position_max_mm))return s->fault=SAFETY_LIMIT;
 uint16_t sg=ds&0x3ffu;s->sg_filtered=(uint16_t)((3u*s->sg_filtered+sg)/4u);
 if(c->stallguard_enabled&&manual&&s->sg_filtered<c->stallguard_warning_level){if(++s->low_count>=c->stallguard_filter_count)return s->fault=SAFETY_STALL;}else s->low_count=0;
 return SAFETY_OK;
}

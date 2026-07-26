#include "stallguard_calibration.h"
void sg_calibration_start(sg_calibration_t*c){*c=(sg_calibration_t){.active=true,.minimum=1023};}
void sg_calibration_add(sg_calibration_t*c,uint16_t v){if(!c->active||v>1023)return;c->sum+=v;c->samples++;if(v<c->minimum)c->minimum=v;if(v>c->maximum)c->maximum=v;}
bool sg_calibration_finish(sg_calibration_t*c,uint16_t*b,uint16_t*w,uint16_t*e){if(!c->active||c->samples<100)return false;uint16_t avg=(uint16_t)(c->sum/c->samples);if(avg<40)return false;*b=avg;*w=(uint16_t)(avg*70u/100u);*e=(uint16_t)(avg*50u/100u);c->active=false;return true;}

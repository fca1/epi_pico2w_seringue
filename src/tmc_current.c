#include "tmc_current.h"
#include <math.h>
uint8_t tmc_current_scale_from_mA(uint16_t mA,float r){if(!mA||r<=0)return 0;float raw=(mA*32.0f*1.41421356f*(r+0.02f)/325.0f)-1.0f;if(raw<0)raw=0;if(raw>31)raw=31;return(uint8_t)(raw+0.5f);}
uint16_t tmc_current_mA_from_scale(uint8_t cs,float r){if(r<=0)return 0;if(cs>31)cs=31;float mA=((cs+1.0f)/32.0f)*325.0f/(r+0.02f)/1.41421356f;return(uint16_t)(mA+0.5f);}

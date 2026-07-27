#include "tmc_current.h"
#include <math.h>
uint16_t tmc_internal_full_scale_mA(float rref){if(rref<=0)return 0;float peak_mA=5000.0f*3000.0f/(rref+1000.0f);return(uint16_t)(peak_mA/1.41421356f+0.5f);}
uint8_t tmc_internal_current_scale_from_mA(uint16_t mA,float rref){uint16_t full=tmc_internal_full_scale_mA(rref);if(!mA||!full)return 0;float raw=(mA*32.0f/full)-1.0f;if(raw<0)raw=0;if(raw>31)raw=31;return(uint8_t)(raw+0.5f);}
uint16_t tmc_internal_current_mA_from_scale(uint8_t cs,float rref){uint16_t full=tmc_internal_full_scale_mA(rref);if(!full)return 0;if(cs>31)cs=31;return(uint16_t)(((cs+1u)*full+16u)/32u);}

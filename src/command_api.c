#include "command_api.h"
#include <stdlib.h>
#include <string.h>
static float number(const char*s,const char*k){const char*p=strstr(s,k);if(!p)return 0;p=strchr(p,':');return p?strtof(p+1,0):0;}
bool command_parse_json(const char*j,size_t len,machine_command_t*o){if(!j||!o||!len||len>511)return false;char b[512];memcpy(b,j,len);b[len]=0;*o=(machine_command_t){0};
 struct{const char*n;command_kind_t k;}map[]={{"push_start",CMD_PUSH_START},{"push_stop",CMD_PUSH_STOP},{"pull_start",CMD_PULL_START},{"pull_stop",CMD_PULL_STOP},{"stop",CMD_STOP},{"set_trigger_dose",CMD_SET_TRIGGER_DOSE},{"flush_statistics",CMD_FLUSH_STATISTICS},{"unload_syringe",CMD_UNLOAD_SYRINGE},{"dose",CMD_DOSE},{"move_relative",CMD_MOVE_RELATIVE},{"set_zero",CMD_SET_ZERO},{"reset",CMD_RESET},{"sg_calibrate_start",CMD_SG_CAL_START},{"sg_calibrate_finish",CMD_SG_CAL_FINISH},{"sg_calibrate_cancel",CMD_SG_CAL_CANCEL}};
 for(unsigned i=0;i<sizeof(map)/sizeof(map[0]);i++){
  if(strstr(b,map[i].n)){o->kind=map[i].k;break;}
 }
 if(!o->kind)return false;
 o->distance_mm=number(b,"distance_mm");o->speed_mm_s=number(b,"speed_mm_s");o->retract_mm=number(b,"retract_mm");return true;}

#include "command_api.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
static float number(const char*s,const char*k){const char*p=strstr(s,k);if(!p)return 0;p=strchr(p,':');return p?strtof(p+1,0):0;}
static bool same_name(const char*a,const char*b){while(*a&&*b){if(tolower((unsigned char)*a++)!=tolower((unsigned char)*b++))return false;}return *a==*b;}
static config_param_t parameter(const char*s){struct{const char*n;config_param_t p;}map[]={
 {"screw_pitch_mm",CFG_SCREW_PITCH_MM},{"motor_steps_per_rev",CFG_MOTOR_STEPS_PER_REV},{"microsteps",CFG_MICROSTEPS},
 {"motor_run_current_mA",CFG_RUN_CURRENT_MA},{"motor_hold_current_mA",CFG_HOLD_CURRENT_MA},{"manual_speed_mm_s",CFG_MANUAL_SPEED_MM_S},
 {"dosing_speed_mm_s",CFG_DOSING_SPEED_MM_S},{"trigger_dose_mm",CFG_TRIGGER_DOSE_MM},
 {"a1_mm_s2",CFG_A1_MM_S2},{"amax_mm_s2",CFG_AMAX_MM_S2},{"dmax_mm_s2",CFG_DMAX_MM_S2},{"d1_mm_s2",CFG_D1_MM_S2},
 {"retract_distance_mm",CFG_RETRACT_DISTANCE_MM},{"retract_speed_mm_s",CFG_RETRACT_SPEED_MM_S},{"retract_delay_ms",CFG_RETRACT_DELAY_MS},
 {"position_min_mm",CFG_POSITION_MIN_MM},{"position_max_mm",CFG_POSITION_MAX_MM},{"manual_timeout_ms",CFG_MANUAL_TIMEOUT_MS},
 {"stallguard_threshold",CFG_STALLGUARD_THRESHOLD},{"stallguard_warning_level",CFG_STALLGUARD_WARNING},{"stallguard_critical_level",CFG_STALLGUARD_CRITICAL},
 {"stallguard_filter_count",CFG_STALLGUARD_FILTER_COUNT},{"stallguard_enabled",CFG_STALLGUARD_ENABLED}};
 for(unsigned i=0;i<sizeof(map)/sizeof(map[0]);i++){if(same_name(s,map[i].n))return map[i].p;}
 return CFG_NONE;}
bool command_parse_json(const char*j,size_t len,machine_command_t*o){if(!j||!o||!len||len>511)return false;char b[512];memcpy(b,j,len);b[len]=0;*o=(machine_command_t){0};
 struct{const char*n;command_kind_t k;}map[]={{"push_start",CMD_PUSH_START},{"push_stop",CMD_PUSH_STOP},{"pull_start",CMD_PULL_START},{"pull_stop",CMD_PULL_STOP},{"stop",CMD_STOP},{"set_trigger_dose",CMD_SET_TRIGGER_DOSE},{"set_config",CMD_SET_CONFIG},{"flush_statistics",CMD_FLUSH_STATISTICS},{"unload_syringe",CMD_UNLOAD_SYRINGE},{"dose",CMD_DOSE},{"move_relative",CMD_MOVE_RELATIVE},{"set_zero",CMD_SET_ZERO},{"reboot",CMD_REBOOT},{"reset",CMD_RESET},{"sg_calibrate_start",CMD_SG_CAL_START},{"sg_calibrate_finish",CMD_SG_CAL_FINISH},{"sg_calibrate_cancel",CMD_SG_CAL_CANCEL}};
 for(unsigned i=0;i<sizeof(map)/sizeof(map[0]);i++){
  if(strstr(b,map[i].n)){o->kind=map[i].k;break;}
 }
 if(!o->kind)return false;
 o->distance_mm=number(b,"distance_mm");o->speed_mm_s=number(b,"speed_mm_s");o->retract_mm=number(b,"retract_mm");o->value=number(b,"value");
 if(o->kind==CMD_SET_CONFIG){const char*k=strstr(b,"\"parameter\"");if(!k||(k=strchr(k,':'))==NULL)return false;while(*++k&&(*k==' '||*k=='\"'));char name[40];size_t n=0;while(*k&&*k!='\"'&&*k!=','&&n<sizeof(name)-1)name[n++]=*k++;name[n]=0;o->parameter=parameter(name);if(o->parameter==CFG_NONE)return false;}return true;}

bool command_parse_ascii(const char*line,size_t len,machine_command_t*out){if(!line||!out||!len||len>159)return false;char b[160];memcpy(b,line,len);b[len]=0;for(size_t i=0;i<len;i++)b[i]=(char)toupper((unsigned char)b[i]);*out=(machine_command_t){0};
 char*save=NULL;char*cmd=strtok_r(b," \t",&save);if(!cmd)return false;
 if(!strcmp(cmd,"PUSH"))out->kind=CMD_PUSH_START;else if(!strcmp(cmd,"PULL"))out->kind=CMD_PULL_START;else if(!strcmp(cmd,"STOP"))out->kind=CMD_STOP;
 else if(!strcmp(cmd,"ZERO"))out->kind=CMD_SET_ZERO;else if(!strcmp(cmd,"RESET"))out->kind=CMD_REBOOT;else if(!strcmp(cmd,"BOOTSEL"))out->kind=CMD_BOOTSEL;else if(!strcmp(cmd,"FAULTRESET"))out->kind=CMD_RESET;
 else if(!strcmp(cmd,"FLUSH"))out->kind=CMD_FLUSH_STATISTICS;else if(!strcmp(cmd,"DOSE")){out->kind=CMD_DOSE;char*v=strtok_r(NULL," \t",&save);out->distance_mm=v?strtof(v,NULL):0.0f;if(v&&out->distance_mm<=0)return false;v=strtok_r(NULL," \t",&save);if(v)out->speed_mm_s=strtof(v,NULL);v=strtok_r(NULL," \t",&save);out->retract_mm=v?strtof(v,NULL):-1.0f;}
 else if(!strcmp(cmd,"MOVE")){out->kind=CMD_MOVE_RELATIVE;char*v=strtok_r(NULL," \t",&save);if(!v)return false;out->distance_mm=strtof(v,NULL);v=strtok_r(NULL," \t",&save);if(v)out->speed_mm_s=strtof(v,NULL);}
 else if(!strcmp(cmd,"UNLOAD")){out->kind=CMD_UNLOAD_SYRINGE;char*v=strtok_r(NULL," \t",&save);if(v)out->speed_mm_s=strtof(v,NULL);}
 else if(!strcmp(cmd,"SGCAL")){char*v=strtok_r(NULL," \t",&save);if(!v)return false;if(!strcmp(v,"START"))out->kind=CMD_SG_CAL_START;else if(!strcmp(v,"FINISH"))out->kind=CMD_SG_CAL_FINISH;else if(!strcmp(v,"CANCEL"))out->kind=CMD_SG_CAL_CANCEL;else return false;}
 else if(!strcmp(cmd,"SET")){char*n=strtok_r(NULL," \t",&save),*v=strtok_r(NULL," \t",&save);if(!n||!v)return false;for(char*p=n;*p;p++)*p=(char)tolower((unsigned char)*p);out->parameter=parameter(n);out->value=strtof(v,NULL);out->kind=out->parameter==CFG_NONE?CMD_NONE:CMD_SET_CONFIG;}
 return out->kind!=CMD_NONE;}

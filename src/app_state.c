#include "app_state.h"
void app_state_init(app_machine_t *m){m->state=APP_BOOT;m->fault_code=0;}
bool app_state_dispatch(app_machine_t *m, app_event_t e){
  if(e==EVT_FAULT){m->state=APP_FAULT;return true;}
  if(e==EVT_STOP&&m->state!=APP_FAULT){m->state=APP_STOPPING;return true;}
  switch(m->state){
  case APP_BOOT: if(e==EVT_INIT_OK){m->state=APP_READY;return true;} break;
  case APP_READY:
    if(e==EVT_PUSH){m->state=APP_MANUAL_PUSH;return true;}
    if(e==EVT_PULL){m->state=APP_MANUAL_PULL;return true;}
    if(e==EVT_DOSE){m->state=APP_DOSING;return true;}
    if(e==EVT_HOME){m->state=APP_HOMING;return true;} break;
  case APP_MANUAL_PUSH: case APP_MANUAL_PULL:
    if(e==EVT_RELEASE){m->state=APP_STOPPING;return true;} break;
  case APP_DOSING: if(e==EVT_MOVE_DONE){m->state=APP_RETRACTING;return true;} break;
  case APP_RETRACTING: case APP_HOMING:
    if(e==EVT_MOVE_DONE){m->state=APP_READY;return true;} break;
  case APP_STOPPING: if(e==EVT_STOPPED){m->state=APP_READY;return true;} break;
  case APP_FAULT: if(e==EVT_RESET){m->fault_code=0;m->state=APP_READY;return true;} break;
  default: break;
  } return false;
}
const char *app_state_name(app_state_t s){
  static const char *n[]={"BOOT","READY","MANUAL_PUSH",
    "MANUAL_PULL","DOSING","RETRACTING","HOMING","STOPPING","FAULT"};
  return (unsigned)s<sizeof(n)/sizeof(n[0])?n[s]:"UNKNOWN";
}

#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef enum { APP_BOOT, APP_WIFI_CONNECTING, APP_BLE_PROVISIONING, APP_READY,
  APP_MANUAL_PUSH, APP_MANUAL_PULL, APP_DOSING, APP_RETRACTING, APP_HOMING,
  APP_STOPPING, APP_FAULT } app_state_t;
typedef enum { EVT_INIT_OK, EVT_PUSH, EVT_PULL, EVT_RELEASE, EVT_DOSE,
  EVT_MOVE_DONE, EVT_STOPPED, EVT_HOME, EVT_STOP, EVT_FAULT, EVT_RESET } app_event_t;
typedef struct { app_state_t state; uint32_t fault_code; } app_machine_t;
void app_state_init(app_machine_t *);
bool app_state_dispatch(app_machine_t *, app_event_t);
const char *app_state_name(app_state_t);

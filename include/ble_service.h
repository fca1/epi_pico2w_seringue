#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "command_api.h"
bool ble_service_init(void);
bool ble_service_take_command(machine_command_t *command);
bool ble_service_connected(void);
bool ble_service_operational(void);
uint8_t ble_service_advertising_status(void);

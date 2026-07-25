#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "command_api.h"
bool ble_service_init(void);
bool ble_service_take_command(machine_command_t *command);
void ble_service_publish(const char *json);
bool ble_service_connected(void);

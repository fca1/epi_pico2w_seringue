#pragma once
#include <stdbool.h>
#include "device_config.h"
bool config_store_load(device_config_t *cfg);
bool config_store_save(device_config_t *cfg);
bool config_store_factory_reset(void);

#pragma once
#include "pico.h"
#include "hardware/flash.h"
/* Keep the Pico 2 W BTstack reservation on both board variants. */
#define DISPENSER_BTSTACK_SIZE (2u * FLASH_SECTOR_SIZE)
#if PICO_RP2350 && PICO_RP2350_A2_SUPPORTED
#define DISPENSER_BTSTACK_OFFSET (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE - DISPENSER_BTSTACK_SIZE)
#else
#define DISPENSER_BTSTACK_OFFSET (PICO_FLASH_SIZE_BYTES - DISPENSER_BTSTACK_SIZE)
#endif

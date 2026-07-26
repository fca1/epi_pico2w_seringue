#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct { bool push,pull,dose,conflict; } button_state_t;
void buttons_init(void);
button_state_t buttons_update(uint32_t now_ms);

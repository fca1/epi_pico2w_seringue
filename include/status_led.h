#pragma once
#include <stdbool.h>
#include <stdint.h>
void status_led_init(bool radio_ready);
void status_led_update(uint32_t now_ms,bool connected,bool motor_moving);

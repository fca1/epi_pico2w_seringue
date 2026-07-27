#pragma once
#include <stdbool.h>
#include <stddef.h>

bool wifi_uart_parse(const char *text,size_t length,char ssid[33],char password[65]);

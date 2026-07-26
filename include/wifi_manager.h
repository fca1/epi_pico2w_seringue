#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "command_api.h"
#include "device_config.h"
typedef enum {WIFI_IDLE,WIFI_CONNECTING,WIFI_CONNECTED,WIFI_AUTH_FAILED,WIFI_NOT_FOUND,WIFI_TIMEOUT} wifi_state_t;
void wifi_manager_init(const device_config_t *config);
bool wifi_manager_set_ssid(const uint8_t *data,size_t len);
bool wifi_manager_set_password(const uint8_t *data,size_t len);
void wifi_manager_open_provisioning(uint32_t duration_ms);bool wifi_manager_provisioning_open(void);
bool wifi_manager_request_connect(void);
bool wifi_manager_request_scan(void);const char *wifi_manager_scan_results(void);
wifi_state_t wifi_manager_state(void);const char *wifi_manager_state_name(void);const char *wifi_manager_ip(void);
bool wifi_manager_take_command(machine_command_t *command);void wifi_manager_publish(const char *json);

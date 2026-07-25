#pragma once
#include <stdbool.h>
#include <stddef.h>
typedef enum {CMD_NONE,CMD_PUSH_START,CMD_PUSH_STOP,CMD_PULL_START,CMD_PULL_STOP,CMD_STOP,CMD_DOSE,CMD_MOVE_RELATIVE,CMD_SET_ZERO,CMD_RESET} command_kind_t;
typedef struct {command_kind_t kind;float distance_mm,speed_mm_s,retract_mm;} machine_command_t;
bool command_parse_json(const char *json,size_t len,machine_command_t*out);

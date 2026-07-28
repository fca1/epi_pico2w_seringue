#pragma once
#include <stdbool.h>
#include <stddef.h>

void app_tasks_start(void);
bool app_tasks_format_query(const char *command, char *response, size_t capacity);

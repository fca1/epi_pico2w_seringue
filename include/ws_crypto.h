#pragma once
#include <stdbool.h>
#include <stddef.h>
bool ws_crypto_accept(const char *client_key,size_t key_len,char output[29]);

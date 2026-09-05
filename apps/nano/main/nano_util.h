#pragma once

#include <stdint.h>

void nano_string_clear(char *buffer, uint32_t size);
void nano_string_set(char *buffer, uint32_t size, const char *text);
void nano_string_append(char *buffer, uint32_t size, const char *text);
void nano_string_append_u32(char *buffer, uint32_t size, uint32_t value);

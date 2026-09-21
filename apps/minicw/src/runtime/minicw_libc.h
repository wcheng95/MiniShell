#pragma once
#include <stddef.h>
/* Allocation-free formatter subset, statically linked into the app. */
int snprintf(char *destination, size_t capacity, const char *format, ...);

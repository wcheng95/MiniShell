#pragma once
#include <stddef.h>
#include <stdint.h>

static inline size_t keyer_text_len(const char *s)
{
    size_t n = 0;
    while (s[n]) ++n;
    return n;
}
static inline void keyer_text_copy(char *dst, size_t capacity, const char *src)
{
    size_t n = 0;
    while (n + 1 < capacity && src[n]) { dst[n] = src[n]; ++n; }
    if (capacity) dst[n] = 0;
}
static inline void keyer_text_number(char *dst, uint32_t value)
{
    char reverse[10];
    unsigned n = 0;
    do { reverse[n++] = (char)('0' + value % 10u); value /= 10u; } while (value);
    unsigned i = 0;
    while (n) dst[i++] = reverse[--n];
    dst[i] = 0;
}

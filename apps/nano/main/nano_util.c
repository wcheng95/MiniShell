#include "nano_util.h"

#include <stddef.h>
#include <stdint.h>

void *memcpy(void *destination, const void *source, size_t count)
{
    unsigned char *dst = (unsigned char *)destination;
    const unsigned char *src = (const unsigned char *)source;
    for (size_t i = 0u; i < count; ++i) dst[i] = src[i];
    return destination;
}

void *memmove(void *destination, const void *source, size_t count)
{
    unsigned char *dst = (unsigned char *)destination;
    const unsigned char *src = (const unsigned char *)source;
    if (dst == src || count == 0u) return destination;
    if ((uintptr_t)dst < (uintptr_t)src) {
        for (size_t i = 0u; i < count; ++i) dst[i] = src[i];
    } else {
        for (size_t i = count; i > 0u; --i) dst[i - 1u] = src[i - 1u];
    }
    return destination;
}

void *memset(void *destination, int value, size_t count)
{
    unsigned char *dst = (unsigned char *)destination;
    for (size_t i = 0u; i < count; ++i) dst[i] = (unsigned char)value;
    return destination;
}

int memcmp(const void *left, const void *right, size_t count)
{
    const unsigned char *a = (const unsigned char *)left;
    const unsigned char *b = (const unsigned char *)right;
    for (size_t i = 0u; i < count; ++i) {
        if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    }
    return 0;
}

size_t strlen(const char *text)
{
    size_t length = 0u;
    while (text[length] != '\0') ++length;
    return length;
}

static uint32_t string_length_bounded(const char *text, uint32_t max)
{
    uint32_t length = 0u;
    while (length < max && text[length] != '\0') ++length;
    return length;
}

void nano_string_clear(char *buffer, uint32_t size)
{
    if (buffer != NULL && size > 0u) buffer[0] = '\0';
}

void nano_string_set(char *buffer, uint32_t size, const char *text)
{
    if (buffer == NULL || size == 0u) return;
    buffer[0] = '\0';
    nano_string_append(buffer, size, text);
}

void nano_string_append(char *buffer, uint32_t size, const char *text)
{
    if (buffer == NULL || text == NULL || size == 0u) return;
    uint32_t used = string_length_bounded(buffer, size);
    if (used >= size) {
        buffer[size - 1u] = '\0';
        return;
    }
    uint32_t remaining = size - used - 1u;
    uint32_t i = 0u;
    while (i < remaining && text[i] != '\0') {
        buffer[used + i] = text[i];
        ++i;
    }
    buffer[used + i] = '\0';
}

void nano_string_append_u32(char *buffer, uint32_t size, uint32_t value)
{
    char digits[10];
    uint32_t count = 0u;
    do {
        digits[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    } while (value != 0u && count < (uint32_t)sizeof(digits));

    char forward[11];
    uint32_t out = 0u;
    while (count > 0u) forward[out++] = digits[--count];
    forward[out] = '\0';
    nano_string_append(buffer, size, forward);
}

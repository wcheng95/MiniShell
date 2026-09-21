/* Small, allocation-free C subset used by the pinned Mini-CW modules.
 * Linked into the external ELF; never resolved against resident libc. */
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include "minicw_libc.h"
#include <stdlib.h>
#include <string.h>
int atoi(const char *s)
{
    int value = 0, sign = 1;
    while (*s == ' ' || *s == '\t' || *s == '\n') ++s;
    if (*s == '-' || *s == '+') { if (*s == '-') sign = -1; ++s; }
    while (*s >= '0' && *s <= '9') value = value * 10 + *s++ - '0';
    return sign * value;
}
size_t strlen(const char *s) { size_t n = 0; while (s[n]) ++n; return n; }
int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { ++a; ++b; }
    return (unsigned char)*a - (unsigned char)*b;
}
int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *x = a, *y = b;
    for (size_t i = 0; i < n; ++i) if (x[i] != y[i]) return x[i] - y[i];
    return 0;
}
void *memcpy(void *d, const void *s, size_t n)
{
    volatile unsigned char *x = d;
    const volatile unsigned char *y = s;
    for (size_t i = 0; i < n; ++i) x[i] = y[i];
    return d;
}
void *memset(void *d, int c, size_t n)
{
    volatile unsigned char *x = d;
    for (size_t i = 0; i < n; ++i) x[i] = (unsigned char)c;
    return d;
}
void *memmove(void *d, const void *s, size_t n)
{
    unsigned char *x = d;
    const unsigned char *y = s;
    if ((uintptr_t)x < (uintptr_t)y) for (size_t i = 0; i < n; ++i) x[i] = y[i];
    else while (n) { --n; x[n] = y[n]; }
    return d;
}
char *strstr(const char *s, const char *needle)
{
    if (!*needle) return (char *)s;
    for (; *s; ++s) {
        size_t n = 0;
        while (needle[n] && s[n] && needle[n] == s[n]) ++n;
        if (!needle[n]) return (char *)s;
    }
    return NULL;
}
static void emit(char *dst, size_t cap, size_t *count, char ch)
{
    if (cap && *count < cap - 1) dst[*count] = ch;
    ++*count;
}
/* Only %s/%c/%d/%u, width, left alignment and string precision are needed.
 * This deliberately does not bring in floating point, locale, heap or I/O. */
int snprintf(char *dst, size_t cap, const char *fmt, ...)
{
    va_list ap;
    size_t count = 0;
    va_start(ap, fmt);
    while (*fmt) {
        if (*fmt != '%') { emit(dst, cap, &count, *fmt++); continue; }
        ++fmt;
        int left = 0, width = 0, precision = -1;
        if (*fmt == '-') { left = 1; ++fmt; }
        while (*fmt >= '0' && *fmt <= '9') width = width * 10 + *fmt++ - '0';
        if (*fmt == '.') {
            ++fmt;
            precision = 0;
            if (*fmt == '*') { precision = va_arg(ap, int); ++fmt; }
            else while (*fmt >= '0' && *fmt <= '9') precision = precision * 10 + *fmt++ - '0';
        }
        char buf[16];
        const char *value = buf;
        size_t len = 0;
        char type = *fmt;
        if (*fmt) ++fmt;
        if (type == 's') {
            value = va_arg(ap, const char *);
            while (value[len] && (precision < 0 || len < (size_t)precision)) ++len;
        } else if (type == 'd' || type == 'u') {
            unsigned v;
            int negative = 0;
            if (type == 'd') { int iv = va_arg(ap, int); negative = iv < 0; v = negative ? 0U - (unsigned)iv : (unsigned)iv; }
            else v = va_arg(ap, unsigned);
            char reverse[10]; size_t n = 0;
            do { reverse[n++] = (char)('0' + v % 10); v /= 10; } while (v);
            if (negative) buf[len++] = '-';
            while (n) buf[len++] = reverse[--n];
        } else if (type == 'c') { buf[len++] = (char)va_arg(ap, int); }
        else if (type == '%') { buf[len++] = '%'; }
        else { va_end(ap); if (cap) dst[count < cap ? count : cap - 1] = 0; return -1; }
        if (!left) while (width > (int)len) { emit(dst, cap, &count, ' '); --width; }
        for (size_t i = 0; i < len; ++i) emit(dst, cap, &count, value[i]);
        if (left) while (width > (int)len) { emit(dst, cap, &count, ' '); --width; }
    }
    va_end(ap);
    if (cap) dst[count < cap ? count : cap - 1] = '\0';
    return (int)count;
}

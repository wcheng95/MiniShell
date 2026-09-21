#include <stddef.h>

/* Compiler-generated structure copies/initializers must remain inside the
 * external ELF. Volatile byte accesses prevent recursive builtin lowering. */
void *memcpy(void *destination, const void *source, size_t count)
{
    volatile unsigned char *d = destination;
    const volatile unsigned char *s = source;
    for (size_t i = 0; i < count; ++i) d[i] = s[i];
    return destination;
}
void *memset(void *destination, int value, size_t count)
{
    volatile unsigned char *d = destination;
    for (size_t i = 0; i < count; ++i) d[i] = (unsigned char)value;
    return destination;
}

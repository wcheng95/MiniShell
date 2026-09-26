#ifndef FT8_CQ_TOKEN_H
#define FT8_CQ_TOKEN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Standard-message CQ token grammar and n28 representation. Uppercase input. */
static inline bool ft8_cq_modifier_pack(const char *text, size_t length, uint32_t *value)
{
    unsigned letters = 0, digits = 0, modifier = 0, number = 0;
    if (!text || length < 1u || length > 4u) return false;
    for (size_t i = 0; i < length; ++i) {
        if (text[i] >= 'A' && text[i] <= 'Z') {
            ++letters; modifier = modifier * 27u + (unsigned)(text[i] - 'A' + 1);
        } else if (text[i] >= '0' && text[i] <= '9') {
            ++digits; number = number * 10u + (unsigned)(text[i] - '0');
        } else return false;
    }
    uint32_t packed;
    if (!digits && letters) packed = 1003u + modifier;
    else if (digits == 3u && !letters) packed = 3u + number;
    else return false;
    if (value) *value = packed;
    return true;
}
#endif

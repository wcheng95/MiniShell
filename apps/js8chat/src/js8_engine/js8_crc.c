#include "js8_crc.h"

#include <string.h>

static uint16_t crc12(const uint8_t payload[JS8_PAYLOAD_BITS])
{
    uint16_t remainder = 0;
    /* Boost augmented_crc divides the supplied bits directly. The 11-byte
     * upstream buffer already includes 13 zero bits; do not augment again.
     */
    for (unsigned i = 0; i < 88; ++i) {
        unsigned top = remainder & 0x800u;
        remainder = (uint16_t)((remainder << 1) |
                              (i < JS8_PAYLOAD_BITS ? payload[i] : 0u));
        if (top)
            remainder ^= 0xc06u;
        remainder &= 0xfffu;
    }
    return remainder ^ 42u;
}

int js8_crc12_append(const uint8_t payload_bits[JS8_PAYLOAD_BITS],
                     uint8_t info_bits[JS8_INFO_BITS])
{
    if (!payload_bits || !info_bits)
        return -1;
    for (unsigned i = 0; i < JS8_PAYLOAD_BITS; ++i)
        if (payload_bits[i] > 1)
            return -1;
    uint16_t crc = crc12(payload_bits);
    memcpy(info_bits, payload_bits, JS8_PAYLOAD_BITS);
    for (unsigned i = 0; i < JS8_CRC_BITS; ++i)
        info_bits[JS8_PAYLOAD_BITS + i] = (crc >> (11u - i)) & 1u;
    return 0;
}

int js8_crc12_check(const uint8_t info_bits[JS8_INFO_BITS])
{
    if (!info_bits)
        return -1;
    for (unsigned i = 0; i < JS8_INFO_BITS; ++i)
        if (info_bits[i] > 1)
            return -1;
    uint16_t received = 0;
    for (unsigned i = JS8_PAYLOAD_BITS; i < JS8_INFO_BITS; ++i)
        received = (uint16_t)((received << 1) | info_bits[i]);
    return received == crc12(info_bits);
}

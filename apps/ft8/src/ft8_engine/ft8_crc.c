#include "ft8_crc.h"

#define FT8_CRC_POLYNOMIAL ((uint16_t)0x2757u)
#define FT8_CRC_WIDTH 14
#define FT8_CRC_TOPBIT (1u << (FT8_CRC_WIDTH - 1))

uint16_t ft8_crc_compute(const uint8_t message[], int num_bits)
{
    uint16_t remainder = 0;
    int idx_byte = 0;

    for (int idx_bit = 0; idx_bit < num_bits; ++idx_bit) {
        if (idx_bit % 8 == 0) {
            remainder ^= (uint16_t)(message[idx_byte] << (FT8_CRC_WIDTH - 8));
            ++idx_byte;
        }

        if (remainder & FT8_CRC_TOPBIT)
            remainder = (uint16_t)((remainder << 1) ^ FT8_CRC_POLYNOMIAL);
        else
            remainder = (uint16_t)(remainder << 1);
    }

    return (uint16_t)(remainder & ((FT8_CRC_TOPBIT << 1) - 1u));
}

uint16_t ft8_crc_extract(const uint8_t a91[])
{
    return (uint16_t)(((a91[9] & 0x07u) << 11) |
                      (a91[10] << 3) |
                      (a91[11] >> 5));
}

#ifndef FT8_CRC_H
#define FT8_CRC_H

#include <stdint.h>

uint16_t ft8_crc_compute(const uint8_t message[], int num_bits);
uint16_t ft8_crc_extract(const uint8_t a91[]);

#endif

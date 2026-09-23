#ifndef JS8_CRC_H
#define JS8_CRC_H

#include <stdint.h>

#define JS8_PAYLOAD_BITS 75u
#define JS8_CRC_BITS 12u
#define JS8_INFO_BITS 87u

/* Arrays contain 0/1 bits in MSB-first protocol order, not packed bytes.
 * All pointers are required. Invalid inputs return -1 without changing output.
 * Caller supplies fixed-size, non-overlapping input/output storage.
 */
int js8_crc12_append(const uint8_t payload_bits[JS8_PAYLOAD_BITS],
                     uint8_t info_bits[JS8_INFO_BITS]); /* 0 on success */
int js8_crc12_check(const uint8_t info_bits[JS8_INFO_BITS]); /* 1 valid, 0 mismatch */

#endif

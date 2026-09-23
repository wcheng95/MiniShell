#ifndef JS8_LDPC_H
#define JS8_LDPC_H

#include "js8_crc.h"

#define JS8_CODEWORD_BITS 174u
#define JS8_BP_MAX_ROWS 7u
#define JS8_BP_MAX_CHECKS 3u
#define JS8_BP_MAX_ITERATIONS 30u

/* Unpacked 0/1 bits. Codeword = parity[87], information[87].
 * All pointers required; caller owns fixed-size, non-overlapping arrays.
 * Invalid arguments return -1 with outputs unchanged.
 */
int js8_ldpc_encode(const uint8_t info_bits[JS8_INFO_BITS],
                    uint8_t codeword[JS8_CODEWORD_BITS]); /* 0 on success */

/* Positive LLR -> bit 1; negative or zero -> bit 0. LLRs must be finite.
 * Returns 0 on parity-check success, 1 on nonconvergence, -1 on invalid input.
 * On success hard_errors counts decisions opposed to the input LLRs (zero
 * LLRs are not errors). CRC validation is separate. On nonconvergence,
 * codeword holds the last decisions, info_bits is zeroed, hard_errors is -1.
 * Scratch is fixed-size automatic storage; no heap or shared mutable state.
 */
int js8_ldpc_decode(const float llr[JS8_CODEWORD_BITS],
                    uint8_t info_bits[JS8_INFO_BITS],
                    uint8_t codeword[JS8_CODEWORD_BITS], int *hard_errors);

#endif

#ifndef FT8_LDPC_H
#define FT8_LDPC_H

#include <stdint.h>

#define FT8_LDPC_N 174
#define FT8_LDPC_K 91
#define FT8_LDPC_M 83
#define FT8_LDPC_K_BYTES 12

void ft8_ldpc_decode(float codeword[FT8_LDPC_N],
                     int max_iterations,
                     uint8_t plain[FT8_LDPC_N],
                     int *out_errors);

#endif

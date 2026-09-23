#include "js8_ldpc.h"

#include <math.h>
#include <string.h>

#include "js8_ldpc_tables.h"

int js8_ldpc_encode(const uint8_t info_bits[JS8_INFO_BITS],
                    uint8_t codeword[JS8_CODEWORD_BITS])
{
    if (!info_bits || !codeword)
        return -1;
    for (unsigned i = 0; i < JS8_INFO_BITS; ++i)
        if (info_bits[i] > 1)
            return -1;
    for (unsigned row = 0; row < JS8_INFO_BITS; ++row) {
        uint8_t parity = 0;
        for (unsigned col = 0; col < JS8_INFO_BITS; ++col)
            parity ^= info_bits[col] & ((kGenerator[row][col / 8] >>
                                        (7u - col % 8)) & 1u);
        codeword[row] = parity;
    }
    memcpy(codeword + JS8_INFO_BITS, info_bits, JS8_INFO_BITS);
    return 0;
}

int js8_ldpc_decode(const float llr[JS8_CODEWORD_BITS],
                    uint8_t info_bits[JS8_INFO_BITS],
                    uint8_t codeword[JS8_CODEWORD_BITS], int *hard_errors)
{
    if (!llr || !info_bits || !codeword || !hard_errors)
        return -1;
    for (unsigned i = 0; i < JS8_CODEWORD_BITS; ++i)
        if (!isfinite(llr[i]))
            return -1;

    float tov[JS8_CODEWORD_BITS][JS8_BP_MAX_CHECKS] = {{0}};
    float tanhtoc[JS8_INFO_BITS][JS8_BP_MAX_ROWS] = {{0}};
    float zn[JS8_CODEWORD_BITS];
    int stalled = 0;
    int last_checks = 0;
    memset(info_bits, 0, JS8_INFO_BITS);
    *hard_errors = -1;

    /* Upstream checks the initial decisions, then up to 30 BP updates. */
    for (unsigned iter = 0; iter <= JS8_BP_MAX_ITERATIONS; ++iter) {
        for (unsigned i = 0; i < JS8_CODEWORD_BITS; ++i) {
            float sum = 0;
            for (unsigned j = 0; j < JS8_BP_MAX_CHECKS; ++j)
                sum += tov[i][j];
            zn[i] = llr[i] + sum;
            codeword[i] = zn[i] > 0;
        }
        int checks = 0;
        for (unsigned i = 0; i < JS8_INFO_BITS; ++i) {
            unsigned syndrome = 0;
            for (unsigned j = 0; j < kCheckLengths[i]; ++j)
                syndrome ^= codeword[kCheckBits[i][j]];
            checks += (int)syndrome;
        }
        if (!checks) {
            memcpy(info_bits, codeword + JS8_INFO_BITS, JS8_INFO_BITS);
            *hard_errors = 0;
            for (unsigned i = 0; i < JS8_CODEWORD_BITS; ++i)
                *hard_errors += (2 * (int)codeword[i] - 1) * llr[i] < 0;
            return 0;
        }
        if (iter > 0) {
            stalled = checks < last_checks ? 0 : stalled + 1;
            if (stalled >= 5 && iter >= 10 && checks > 15)
                return 1;
        }
        last_checks = checks;
        /* Fuse upstream toc and tanhtoc storage; edge order is unchanged. */
        for (unsigned i = 0; i < JS8_INFO_BITS; ++i) {
            for (unsigned j = 0; j < kCheckLengths[i]; ++j) {
                unsigned bit = kCheckBits[i][j];
                float message = zn[bit];
                for (unsigned k = 0; k < JS8_BP_MAX_CHECKS; ++k)
                    if (kBitChecks[bit][k] == i)
                        message -= tov[bit][k];
                tanhtoc[i][j] = tanhf(-message / 2.0f);
            }
        }
        for (unsigned i = 0; i < JS8_CODEWORD_BITS; ++i) {
            for (unsigned j = 0; j < JS8_BP_MAX_CHECKS; ++j) {
                unsigned check = kBitChecks[i][j];
                float product = 1;
                for (unsigned k = 0; k < kCheckLengths[check]; ++k)
                    if (kCheckBits[check][k] != i)
                        product *= tanhtoc[check][k];
                tov[i][j] = 2.0f * atanhf(-product);
            }
        }
    }
    return 1;
}

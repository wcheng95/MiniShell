#include "ft8_ldpc.h"

#include <stddef.h>
#include <string.h>

static const uint8_t kParityChecks[FT8_LDPC_M][7] = {
    { 4, 31, 59, 91, 92, 96, 153 },
    { 5, 32, 60, 93, 115, 146, 0 },
    { 6, 24, 61, 94, 122, 151, 0 },
    { 7, 33, 62, 95, 96, 143, 0 },
    { 8, 25, 63, 83, 93, 96, 148 },
    { 6, 32, 64, 97, 126, 138, 0 },
    { 5, 34, 65, 78, 98, 107, 154 },
    { 9, 35, 66, 99, 139, 146, 0 },
    { 10, 36, 67, 100, 107, 126, 0 },
    { 11, 37, 67, 87, 101, 139, 158 },
    { 12, 38, 68, 102, 105, 155, 0 },
    { 13, 39, 69, 103, 149, 162, 0 },
    { 8, 40, 70, 82, 104, 114, 145 },
    { 14, 41, 71, 88, 102, 123, 156 },
    { 15, 42, 59, 106, 123, 159, 0 },
    { 1, 33, 72, 106, 107, 157, 0 },
    { 16, 43, 73, 108, 141, 160, 0 },
    { 17, 37, 74, 81, 109, 131, 154 },
    { 11, 44, 75, 110, 121, 166, 0 },
    { 45, 55, 64, 111, 130, 161, 173 },
    { 8, 46, 71, 112, 119, 166, 0 },
    { 18, 36, 76, 89, 113, 114, 143 },
    { 19, 38, 77, 104, 116, 163, 0 },
    { 20, 47, 70, 92, 138, 165, 0 },
    { 2, 48, 74, 113, 128, 160, 0 },
    { 21, 45, 78, 83, 117, 121, 151 },
    { 22, 47, 58, 118, 127, 164, 0 },
    { 16, 39, 62, 112, 134, 158, 0 },
    { 23, 43, 79, 120, 131, 145, 0 },
    { 19, 35, 59, 73, 110, 125, 161 },
    { 20, 36, 63, 94, 136, 161, 0 },
    { 14, 31, 79, 98, 132, 164, 0 },
    { 3, 44, 80, 124, 127, 169, 0 },
    { 19, 46, 81, 117, 135, 167, 0 },
    { 7, 49, 58, 90, 100, 105, 168 },
    { 12, 50, 61, 118, 119, 144, 0 },
    { 13, 51, 64, 114, 118, 157, 0 },
    { 24, 52, 76, 129, 148, 149, 0 },
    { 25, 53, 69, 90, 101, 130, 156 },
    { 20, 46, 65, 80, 120, 140, 170 },
    { 21, 54, 77, 100, 140, 171, 0 },
    { 35, 82, 133, 142, 171, 174, 0 },
    { 14, 30, 83, 113, 125, 170, 0 },
    { 4, 29, 68, 120, 134, 173, 0 },
    { 1, 4, 52, 57, 86, 136, 152 },
    { 26, 51, 56, 91, 122, 137, 168 },
    { 52, 84, 110, 115, 145, 168, 0 },
    { 7, 50, 81, 99, 132, 173, 0 },
    { 23, 55, 67, 95, 172, 174, 0 },
    { 26, 41, 77, 109, 141, 148, 0 },
    { 2, 27, 41, 61, 62, 115, 133 },
    { 27, 40, 56, 124, 125, 126, 0 },
    { 18, 49, 55, 124, 141, 167, 0 },
    { 6, 33, 85, 108, 116, 156, 0 },
    { 28, 48, 70, 85, 105, 129, 158 },
    { 9, 54, 63, 131, 147, 155, 0 },
    { 22, 53, 68, 109, 121, 174, 0 },
    { 3, 13, 48, 78, 95, 123, 0 },
    { 31, 69, 133, 150, 155, 169, 0 },
    { 12, 43, 66, 89, 97, 135, 159 },
    { 5, 39, 75, 102, 136, 167, 0 },
    { 2, 54, 86, 101, 135, 164, 0 },
    { 15, 56, 87, 108, 119, 171, 0 },
    { 10, 44, 82, 91, 111, 144, 149 },
    { 23, 34, 71, 94, 127, 153, 0 },
    { 11, 49, 88, 92, 142, 157, 0 },
    { 29, 34, 87, 97, 147, 162, 0 },
    { 30, 50, 60, 86, 137, 142, 162 },
    { 10, 53, 66, 84, 112, 128, 165 },
    { 22, 57, 85, 93, 140, 159, 0 },
    { 28, 32, 72, 103, 132, 166, 0 },
    { 28, 29, 84, 88, 117, 143, 150 },
    { 1, 26, 45, 80, 128, 147, 0 },
    { 17, 27, 89, 103, 116, 153, 0 },
    { 51, 57, 98, 163, 165, 172, 0 },
    { 21, 37, 73, 138, 152, 169, 0 },
    { 16, 47, 76, 130, 137, 154, 0 },
    { 3, 24, 30, 72, 104, 139, 0 },
    { 9, 40, 90, 106, 134, 151, 0 },
    { 15, 58, 60, 74, 111, 150, 163 },
    { 18, 42, 79, 144, 146, 152, 0 },
    { 25, 38, 65, 99, 122, 160, 0 },
    { 17, 42, 75, 129, 170, 172, 0 }
};

static int parity_row_length(int row)
{
    int len = 0;
    while (len < 7 && kParityChecks[row][len] != 0)
        ++len;
    return len;
}

static int build_reverse_checks(uint8_t reverse[FT8_LDPC_N][3])
{
    uint8_t counts[FT8_LDPC_N];
    memset(reverse, 0, FT8_LDPC_N * 3u * sizeof(uint8_t));
    memset(counts, 0, sizeof(counts));

    for (int m = 0; m < FT8_LDPC_M; ++m) {
        int len = parity_row_length(m);
        for (int j = 0; j < len; ++j) {
            int n = (int)kParityChecks[m][j] - 1;
            if (n < 0 || n >= FT8_LDPC_N || counts[n] >= 3)
                return 0;
            reverse[n][counts[n]++] = (uint8_t)(m + 1);
        }
    }

    for (int n = 0; n < FT8_LDPC_N; ++n) {
        if (counts[n] != 3)
            return 0;
    }
    return 1;
}

static int ldpc_check(const uint8_t codeword[FT8_LDPC_N])
{
    int errors = 0;
    for (int m = 0; m < FT8_LDPC_M; ++m) {
        uint8_t x = 0;
        int len = parity_row_length(m);
        for (int i = 0; i < len; ++i)
            x ^= codeword[kParityChecks[m][i] - 1];
        if (x != 0)
            ++errors;
    }
    return errors;
}

static float fast_tanh(float x)
{
    if (x < -4.97f)
        return -1.0f;
    if (x > 4.97f)
        return 1.0f;

    float x2 = x * x;
    float a = x * (945.0f + x2 * (105.0f + x2));
    float b = 945.0f + x2 * (420.0f + x2 * 15.0f);
    return a / b;
}

static float fast_atanh(float x)
{
    float x2 = x * x;
    float a = x * (945.0f + x2 * (-735.0f + x2 * 64.0f));
    float b = 945.0f + x2 * (-1050.0f + x2 * 225.0f);
    return a / b;
}

void ft8_ldpc_decode(float codeword[FT8_LDPC_N],
                     int max_iterations,
                     uint8_t plain[FT8_LDPC_N],
                     int *out_errors)
{
    float tov[FT8_LDPC_N][3];
    float toc[FT8_LDPC_M][7];
    uint8_t reverse[FT8_LDPC_N][3];
    int min_errors = FT8_LDPC_M;

    if (!codeword || !plain || !out_errors || max_iterations <= 0 ||
        !build_reverse_checks(reverse)) {
        if (plain)
            memset(plain, 0, FT8_LDPC_N * sizeof(uint8_t));
        if (out_errors)
            *out_errors = FT8_LDPC_M;
        return;
    }

    for (int n = 0; n < FT8_LDPC_N; ++n)
        tov[n][0] = tov[n][1] = tov[n][2] = 0;

    for (int iter = 0; iter < max_iterations; ++iter) {
        int plain_sum = 0;
        for (int n = 0; n < FT8_LDPC_N; ++n) {
            plain[n] = ((codeword[n] + tov[n][0] + tov[n][1] + tov[n][2]) > 0) ? 1 : 0;
            plain_sum += plain[n];
        }

        if (plain_sum == 0)
            break;

        int errors = ldpc_check(plain);
        if (errors < min_errors) {
            min_errors = errors;
            if (errors == 0)
                break;
        }

        for (int m = 0; m < FT8_LDPC_M; ++m) {
            int len = parity_row_length(m);
            for (int n_idx = 0; n_idx < len; ++n_idx) {
                int n = kParityChecks[m][n_idx] - 1;
                float Tnm = codeword[n];
                for (int m_idx = 0; m_idx < 3; ++m_idx) {
                    if (((int)reverse[n][m_idx] - 1) != m)
                        Tnm += tov[n][m_idx];
                }
                toc[m][n_idx] = fast_tanh(-Tnm / 2);
            }
        }

        for (int n = 0; n < FT8_LDPC_N; ++n) {
            for (int m_idx = 0; m_idx < 3; ++m_idx) {
                int m = reverse[n][m_idx] - 1;
                float Tmn = 1.0f;
                int len = parity_row_length(m);
                for (int n_idx = 0; n_idx < len; ++n_idx) {
                    if (((int)kParityChecks[m][n_idx] - 1) != n)
                        Tmn *= toc[m][n_idx];
                }
                tov[n][m_idx] = -2 * fast_atanh(Tmn);
            }
        }
    }

    *out_errors = min_errors;
}

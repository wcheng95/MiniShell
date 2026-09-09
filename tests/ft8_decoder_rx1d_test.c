#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ft8_decoder.h"

static int failures = 0;

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        ++failures; \
    } \
} while (0)

static void test_candidate_search_contract(void)
{
    uint8_t mag[16] = {0};
    Ft8WaterfallView wf = {
        .mag = mag,
        .max_blocks = 1,
        .num_blocks = 1,
        .num_bins = 16,
        .time_osr = 1,
        .freq_osr = 1,
        .block_stride = 16,
    };
    Ft8Candidate candidates[3];
    size_t count = 99u;

    CHECK(ft8_decoder_find_candidates(&wf, candidates, 3, 1, &count) == FT8_DECODER_OK);
    CHECK(count == 0u);

    CHECK(ft8_decoder_find_candidates(&wf, candidates, 3, 0, &count) == FT8_DECODER_OK);
    CHECK(count == 3u);
    for (size_t i = 0; i < count; ++i)
        CHECK(candidates[i].score == 0);

    count = 99u;
    CHECK(ft8_decoder_find_candidates(NULL, candidates, 3, 0, &count) == FT8_DECODER_ERR_INVALID);
    CHECK(count == 0u);
    CHECK(ft8_decoder_find_candidates(&wf, candidates, 0, 0, &count) == FT8_DECODER_ERR_INVALID);
}

static void test_decode_failure_is_explicit(void)
{
    uint8_t mag[79 * 16];
    Ft8WaterfallView wf = {
        .mag = mag,
        .max_blocks = 79,
        .num_blocks = 79,
        .num_bins = 16,
        .time_osr = 1,
        .freq_osr = 1,
        .block_stride = 16,
    };
    Ft8Candidate candidate = {
        .score = 0,
        .time_offset = 0,
        .freq_offset = 0,
        .time_sub = 0,
        .freq_sub = 0,
    };
    Ft8DecodedPayload decoded;

    memset(mag, 0, sizeof(mag));
    memset(&decoded, 0xA5, sizeof(decoded));
    CHECK(ft8_decoder_decode_candidate(&wf, &candidate, 25, &decoded) == FT8_DECODER_ERR_LDPC);
    CHECK(decoded.ldpc_errors > 0);
    CHECK(memcmp(&decoded.candidate, &candidate, sizeof(candidate)) == 0);

    CHECK(ft8_decoder_decode_candidate(&wf, &candidate, 0, &decoded) == FT8_DECODER_ERR_INVALID);
    CHECK(ft8_decoder_decode_candidate(NULL, &candidate, 25, &decoded) == FT8_DECODER_ERR_INVALID);
}

static void test_waterfall_validation(void)
{
    uint8_t mag[16] = {0};
    Ft8WaterfallView wf = {
        .mag = mag,
        .max_blocks = 1,
        .num_blocks = 1,
        .num_bins = 16,
        .time_osr = 1,
        .freq_osr = 1,
        .block_stride = 15,
    };
    Ft8Candidate candidate;
    size_t count = 0u;

    CHECK(ft8_decoder_find_candidates(&wf, &candidate, 1, 0, &count) == FT8_DECODER_ERR_INVALID);
}

int main(void)
{
    CHECK(FT8_DECODER_CANDIDATE_CAPACITY == 50u);
    CHECK(FT8_DECODER_MIN_SCORE == 5);
    CHECK(FT8_DECODER_MAX_LDPC_ITERATIONS == 25);
    CHECK(FT8_PAYLOAD_BYTES == 10u);

    test_candidate_search_contract();
    test_decode_failure_is_explicit();
    test_waterfall_validation();

    if (failures != 0) {
        fprintf(stderr, "ft8_decoder_rx1d_test: %d failure(s)\n", failures);
        return 1;
    }

    puts("ft8_decoder_rx1d_test: PASS");
    return 0;
}

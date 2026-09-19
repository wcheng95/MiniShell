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

static void test_negative_time_offset_uses_retained_ring(void)
{
    enum { MAX_BLOCKS = 16, NUM_BINS = 16, FREQ_OFFSET = 4 };
    static const uint8_t costas[7] = {3, 1, 4, 0, 6, 5, 2};
    uint8_t mag[MAX_BLOCKS * NUM_BINS];
    Ft8WaterfallView wf = {
        .mag = mag,
        .max_blocks = MAX_BLOCKS,
        .num_blocks = 10,
        .num_bins = NUM_BINS,
        .time_osr = 1,
        .freq_osr = 1,
        .block_stride = NUM_BINS,
        .anchor_index = 3,
        .first_block = -10,
    };
    Ft8Candidate candidates[FT8_DECODER_CANDIDATE_CAPACITY];
    size_t count = 0u;
    int found = 0;

    memset(mag, 0, sizeof(mag));

    /* Put one complete first Costas group at logical blocks -10..-4. */
    for (int k = 0; k < 7; ++k) {
        int logical = -10 + k;
        int physical = ((int)wf.anchor_index + logical) % (int)wf.max_blocks;
        if (physical < 0) physical += (int)wf.max_blocks;
        mag[(size_t)physical * wf.block_stride +
            FREQ_OFFSET + costas[k]] = 200u;
    }

    CHECK(ft8_decoder_find_candidates(&wf,
                                      candidates,
                                      FT8_DECODER_CANDIDATE_CAPACITY,
                                      5,
                                      &count) == FT8_DECODER_OK);

    for (size_t i = 0u; i < count; ++i) {
        if (candidates[i].time_offset == -10 &&
            candidates[i].time_sub == 0u &&
            candidates[i].freq_offset == FREQ_OFFSET &&
            candidates[i].freq_sub == 0u &&
            candidates[i].score > 0) {
            found = 1;
            break;
        }
    }
    CHECK(found);
}

static void test_incremental_search_matches_synchronous(void)
{
    enum { MAX_BLOCKS = 20, NUM_BINS = 24, CAPACITY = 8 };
    uint8_t mag[MAX_BLOCKS * NUM_BINS];
    Ft8WaterfallView wf = {
        .mag = mag,
        .max_blocks = MAX_BLOCKS,
        .num_blocks = MAX_BLOCKS,
        .num_bins = NUM_BINS,
        .time_osr = 1,
        .freq_osr = 1,
        .block_stride = NUM_BINS,
        .anchor_index = 0,
        .first_block = 0,
    };
    Ft8Candidate synchronous[CAPACITY];
    Ft8Candidate incremental[CAPACITY];
    Ft8CandidateSearchState state;
    size_t synchronous_count = 0u;
    size_t incremental_count = 0u;
    int completed = 0;

    for (size_t i = 0u; i < sizeof(mag); ++i)
        mag[i] = (uint8_t)((i * 37u + i / 7u) & 0xffu);
    memset(synchronous, 0, sizeof(synchronous));
    memset(incremental, 0, sizeof(incremental));

    CHECK(ft8_decoder_find_candidates(&wf, synchronous, CAPACITY, -100,
                                      &synchronous_count) == FT8_DECODER_OK);
    CHECK(ft8_decoder_candidate_search_begin(&state, CAPACITY, -100) ==
          FT8_DECODER_OK);

    for (unsigned step = 0u; step < 2000u && !completed; ++step) {
        CHECK(ft8_decoder_candidate_search_step(&wf, &state, incremental, 7u,
                                                &completed, &incremental_count) ==
              FT8_DECODER_OK);
    }

    CHECK(completed);
    CHECK(incremental_count == synchronous_count);
    CHECK(memcmp(incremental, synchronous,
                 synchronous_count * sizeof(synchronous[0])) == 0);

    completed = 0;
    CHECK(ft8_decoder_candidate_search_step(&wf, &state, incremental, 0u,
                                            &completed, &incremental_count) ==
          FT8_DECODER_ERR_INVALID);
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
    test_negative_time_offset_uses_retained_ring();
    test_incremental_search_matches_synchronous();
    test_decode_failure_is_explicit();
    test_waterfall_validation();

    if (failures != 0) {
        fprintf(stderr, "ft8_decoder_rx1d_test: %d failure(s)\n", failures);
        return 1;
    }

    puts("ft8_decoder_rx1d_test: PASS");
    return 0;
}

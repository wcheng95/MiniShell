#ifndef FT8_DECODER_H
#define FT8_DECODER_H

#include <stddef.h>
#include <stdint.h>

#include "ft8_monitor.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FT8_DECODER_CANDIDATE_CAPACITY 50u
#define FT8_DECODER_MIN_SCORE 5
#define FT8_DECODER_MAX_LDPC_ITERATIONS 25
#define FT8_PAYLOAD_BYTES 10u
#define FT8_DECODER_MAX_SCORE_TERMS 75u

typedef enum {
    FT8_DECODER_OK = 0,
    FT8_DECODER_ERR_INVALID = -1,
    FT8_DECODER_ERR_LDPC = -2,
    FT8_DECODER_ERR_CRC = -3
} Ft8DecoderStatus;

typedef struct {
    int16_t score;
    int16_t time_offset;
    int16_t freq_offset;
    uint8_t time_sub;
    uint8_t freq_sub;
} Ft8Candidate;

/*
 * Resumable candidate-search cursor. The search order and heap policy are
 * identical to ft8_decoder_find_candidates(); only scheduling is different.
 */
typedef struct {
    const uint8_t *positive;
    const uint8_t *negative;
} Ft8CandidateScoreTerm;

typedef struct {
    int initialized;
    int completed;
    size_t capacity;
    size_t heap_size;
    int min_score;
    int16_t time_offset;
    int16_t freq_offset;
    uint8_t time_sub;
    uint8_t freq_sub;

    /* Cache the Costas comparison terms for one time/sub-lane while sweeping
     * the innermost frequency-offset loop. */
    int score_cache_valid;
    int16_t score_cache_time_offset;
    uint8_t score_cache_time_sub;
    uint8_t score_cache_freq_sub;
    uint8_t score_term_count;
    Ft8CandidateScoreTerm score_terms[FT8_DECODER_MAX_SCORE_TERMS];
} Ft8CandidateSearchState;

typedef struct {
    Ft8Candidate candidate;
    int ldpc_errors;
    uint16_t crc_extracted;
    uint16_t crc_calculated;
    uint8_t payload[FT8_PAYLOAD_BYTES];
} Ft8DecodedPayload;

Ft8DecoderStatus ft8_decoder_candidate_search_begin(
    Ft8CandidateSearchState *state,
    size_t capacity,
    int min_score);

Ft8DecoderStatus ft8_decoder_candidate_search_step(
    const Ft8WaterfallView *waterfall,
    Ft8CandidateSearchState *state,
    Ft8Candidate *candidates,
    size_t position_budget,
    int *out_completed,
    size_t *out_count);

Ft8DecoderStatus ft8_decoder_find_candidates(const Ft8WaterfallView *waterfall,
                                             Ft8Candidate *candidates,
                                             size_t capacity,
                                             int min_score,
                                             size_t *out_count);
Ft8DecoderStatus ft8_decoder_decode_candidate(const Ft8WaterfallView *waterfall,
                                              const Ft8Candidate *candidate,
                                              int max_iterations,
                                              Ft8DecodedPayload *out_payload);

#ifdef __cplusplus
}
#endif

#endif

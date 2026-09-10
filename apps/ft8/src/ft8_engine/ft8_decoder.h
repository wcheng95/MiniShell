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

typedef struct {
    Ft8Candidate candidate;
    int ldpc_errors;
    uint16_t crc_extracted;
    uint16_t crc_calculated;
    uint8_t payload[FT8_PAYLOAD_BYTES];

    /* Factual RX metadata populated by Ft8Engine while its waterfall exists. */
    int16_t offset_hz;
    int8_t snr_db;
} Ft8DecodedPayload;

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

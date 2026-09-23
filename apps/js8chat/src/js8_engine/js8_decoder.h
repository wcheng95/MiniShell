#ifndef JS8_DECODER_H
#define JS8_DECODER_H

#include "js8_monitor.h"
#include "js8_ldpc.h"

#define JS8_DECODER_CANDIDATE_CAPACITY 50u
#define JS8_DECODER_MIN_SCORE 5

typedef enum {
    JS8_DECODER_OK = 0,
    JS8_DECODER_ERR_INVALID = -1,
    JS8_DECODER_ERR_LDPC = -2,
    JS8_DECODER_ERR_CRC = -3
} Js8DecoderStatus;

typedef struct {
    int16_t score;
    int16_t time_offset;
    int16_t freq_offset; /* relative to monitor min_bin, in 6.25 Hz bins */
    uint8_t time_sub;
    uint8_t freq_sub;
} Js8Candidate;

typedef struct {
    Js8Candidate candidate;
    int ldpc_errors;
    uint8_t payload_bits[JS8_PAYLOAD_BITS];
} Js8DecodedPayload;

/* Synchronous -10..19 block search, strongest-first output. Capacity is 1..50.
 * No allocations; caller supplies capacity entries. out_count is zero on error.
 * Keep the waterfall immutable throughout search and candidate decoding.
 */
Js8DecoderStatus js8_decoder_find_candidates(const Js8WaterfallView *waterfall,
                                             Js8Candidate *candidates,
                                             size_t capacity, int min_score,
                                             size_t *out_count);

/* Raw max-log dB differences, positive => bit 1, MSB-first/direct binary.
 * Missing time rows are zero erasures. All pointers required; invalid input
 * leaves output unchanged. This diagnostic edge does not normalize likelihoods.
 */
Js8DecoderStatus js8_decoder_extract_likelihood(const Js8WaterfallView *waterfall,
                                               const Js8Candidate *candidate,
                                               float llr[JS8_CODEWORD_BITS]);

/* Normalize likelihoods and apply T052 LDPC/CRC. Invalid input leaves output
 * unchanged; valid attempts expose candidate/hard-error diagnostics and leave
 * payload zero on failure. Only OK makes payload_bits a validated identity.
 * Caller owns distinct, correctly sized input/output storage.
 */
Js8DecoderStatus js8_decoder_decode_candidate(const Js8WaterfallView *waterfall,
                                              const Js8Candidate *candidate,
                                              Js8DecodedPayload *out_payload);

#endif

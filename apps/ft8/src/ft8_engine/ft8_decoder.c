#include "ft8_decoder.h"

#include "ft8_crc.h"
#include "ft8_ldpc.h"

#include <math.h>
#include <string.h>

#define FT8_DATA_SYMBOLS 58
#define FT8_LENGTH_SYNC 7
#define FT8_NUM_SYNC 3
#define FT8_SYNC_OFFSET 36

static const uint8_t kCostasPattern[7] = { 3, 1, 4, 0, 6, 5, 2 };
static const uint8_t kGrayMap[8] = { 0, 1, 3, 2, 5, 6, 4, 7 };

static int waterfall_valid(const Ft8WaterfallView *wf)
{
    int64_t last_block;

    if (!wf || !wf->mag || wf->max_blocks == 0u ||
        wf->num_blocks > wf->max_blocks ||
        wf->anchor_index >= wf->max_blocks ||
        wf->num_bins < 8u || wf->time_osr == 0u || wf->freq_osr == 0u)
        return 0;
    if (wf->block_stride != wf->time_osr * wf->freq_osr * wf->num_bins)
        return 0;

    last_block = (int64_t)wf->first_block + (int64_t)wf->num_blocks;
    return last_block >= (int64_t)wf->first_block;
}

static int waterfall_physical_block(const Ft8WaterfallView *wf,
                                    int logical_block,
                                    uint32_t *out_block)
{
    int64_t last_block;
    int64_t physical;

    if (!waterfall_valid(wf) || !out_block)
        return 0;

    last_block = (int64_t)wf->first_block + (int64_t)wf->num_blocks;
    if ((int64_t)logical_block < (int64_t)wf->first_block ||
        (int64_t)logical_block >= last_block)
        return 0;

    physical = (int64_t)wf->anchor_index + (int64_t)logical_block;
    physical %= (int64_t)wf->max_blocks;
    if (physical < 0)
        physical += (int64_t)wf->max_blocks;

    *out_block = (uint32_t)physical;
    return 1;
}

static const uint8_t *candidate_symbol(const Ft8WaterfallView *wf,
                                       const Ft8Candidate *candidate,
                                       int symbol_index)
{
    int block = candidate->time_offset + symbol_index;
    uint32_t physical_block;
    size_t offset;

    if (!wf || !candidate ||
        candidate->time_sub >= wf->time_osr || candidate->freq_sub >= wf->freq_osr ||
        candidate->freq_offset < 0 || candidate->freq_offset + 7 >= (int)wf->num_bins ||
        !waterfall_physical_block(wf, block, &physical_block))
        return NULL;

    offset = (size_t)physical_block * wf->block_stride;
    offset += (size_t)candidate->time_sub * wf->freq_osr * wf->num_bins;
    offset += (size_t)candidate->freq_sub * wf->num_bins;
    offset += (size_t)candidate->freq_offset;
    return wf->mag + offset;
}

static int score_term_add(Ft8CandidateSearchState *state,
                          const uint8_t *positive,
                          const uint8_t *negative)
{
    if (state == NULL || positive == NULL || negative == NULL ||
        state->score_term_count >= FT8_DECODER_MAX_SCORE_TERMS)
        return 0;

    state->score_terms[state->score_term_count].positive = positive;
    state->score_terms[state->score_term_count].negative = negative;
    ++state->score_term_count;
    return 1;
}

static int prepare_score_terms(const Ft8WaterfallView *wf,
                               Ft8CandidateSearchState *state)
{
    Ft8Candidate base;

    if (!waterfall_valid(wf) || state == NULL)
        return 0;

    if (state->score_cache_valid &&
        state->score_cache_time_offset == state->time_offset &&
        state->score_cache_time_sub == state->time_sub &&
        state->score_cache_freq_sub == state->freq_sub)
        return 1;

    memset(&base, 0, sizeof(base));
    base.time_offset = state->time_offset;
    base.time_sub = state->time_sub;
    base.freq_sub = state->freq_sub;
    base.freq_offset = 0;

    state->score_term_count = 0u;

    for (int m = 0; m < FT8_NUM_SYNC; ++m) {
        for (int k = 0; k < FT8_LENGTH_SYNC; ++k) {
            int symbol = FT8_SYNC_OFFSET * m + k;
            const uint8_t *p8 = candidate_symbol(wf, &base, symbol);
            int sm;

            if (!p8)
                continue;

            sm = kCostasPattern[k];
            if (sm > 0 &&
                !score_term_add(state, p8 + sm, p8 + sm - 1))
                return 0;
            if (sm < 7 &&
                !score_term_add(state, p8 + sm, p8 + sm + 1))
                return 0;

            if (k > 0) {
                const uint8_t *prev = candidate_symbol(wf, &base, symbol - 1);
                if (prev &&
                    !score_term_add(state, p8 + sm, prev + sm))
                    return 0;
            }

            if ((k + 1) < FT8_LENGTH_SYNC) {
                const uint8_t *next = candidate_symbol(wf, &base, symbol + 1);
                if (next &&
                    !score_term_add(state, p8 + sm, next + sm))
                    return 0;
            }
        }
    }

    state->score_cache_time_offset = state->time_offset;
    state->score_cache_time_sub = state->time_sub;
    state->score_cache_freq_sub = state->freq_sub;
    state->score_cache_valid = 1;
    return 1;
}

static int ft8_sync_score_search(const Ft8CandidateSearchState *state,
                                 int freq_offset)
{
    int score = 0;

    if (state == NULL || !state->score_cache_valid ||
        state->score_term_count == 0u)
        return 0;

    for (uint8_t i = 0u; i < state->score_term_count; ++i) {
        score += (int)state->score_terms[i].positive[freq_offset] -
                 (int)state->score_terms[i].negative[freq_offset];
    }

    return score / (int)state->score_term_count;
}

static void heapify_down(Ft8Candidate heap[], size_t heap_size)
{
    size_t current = 0u;
    for (;;) {
        size_t left = 2u * current + 1u;
        size_t right = left + 1u;
        size_t smallest = current;
        Ft8Candidate tmp;

        if (left < heap_size && heap[left].score < heap[smallest].score)
            smallest = left;
        if (right < heap_size && heap[right].score < heap[smallest].score)
            smallest = right;
        if (smallest == current)
            break;

        tmp = heap[smallest];
        heap[smallest] = heap[current];
        heap[current] = tmp;
        current = smallest;
    }
}

static void heapify_up(Ft8Candidate heap[], size_t heap_size)
{
    size_t current = heap_size - 1u;
    while (current > 0u) {
        size_t parent = (current - 1u) / 2u;
        Ft8Candidate tmp;
        if (!(heap[current].score < heap[parent].score))
            break;
        tmp = heap[parent];
        heap[parent] = heap[current];
        heap[current] = tmp;
        current = parent;
    }
}

static void candidate_search_advance(const Ft8WaterfallView *wf,
                                     Ft8CandidateSearchState *state)
{
    ++state->freq_offset;
    if (state->freq_offset + 7 < (int)wf->num_bins)
        return;

    state->freq_offset = 0;
    ++state->time_offset;
    if (state->time_offset < 20)
        return;

    state->time_offset = -10;
    ++state->freq_sub;
    if (state->freq_sub < wf->freq_osr)
        return;

    state->freq_sub = 0u;
    ++state->time_sub;
    if (state->time_sub < wf->time_osr)
        return;

    state->completed = 1;
}

static void candidate_search_sort(Ft8Candidate candidates[], size_t heap_size)
{
    for (size_t len_unsorted = heap_size; len_unsorted > 1u; --len_unsorted) {
        Ft8Candidate tmp = candidates[len_unsorted - 1u];
        candidates[len_unsorted - 1u] = candidates[0];
        candidates[0] = tmp;
        heapify_down(candidates, len_unsorted - 1u);
    }
}

Ft8DecoderStatus ft8_decoder_candidate_search_begin(
    Ft8CandidateSearchState *state,
    size_t capacity,
    int min_score)
{
    if (state == NULL || capacity == 0u)
        return FT8_DECODER_ERR_INVALID;

    memset(state, 0, sizeof(*state));
    state->initialized = 1;
    state->capacity = capacity;
    state->min_score = min_score;
    state->time_offset = -10;
    return FT8_DECODER_OK;
}

Ft8DecoderStatus ft8_decoder_candidate_search_step(
    const Ft8WaterfallView *wf,
    Ft8CandidateSearchState *state,
    Ft8Candidate *candidates,
    size_t position_budget,
    int *out_completed,
    size_t *out_count)
{
    size_t processed = 0u;

    if (out_completed)
        *out_completed = 0;
    if (out_count)
        *out_count = 0u;
    if (!waterfall_valid(wf) || state == NULL || !state->initialized ||
        candidates == NULL || position_budget == 0u ||
        out_completed == NULL || out_count == NULL) {
        return FT8_DECODER_ERR_INVALID;
    }

    if (state->completed) {
        *out_completed = 1;
        *out_count = state->heap_size;
        return FT8_DECODER_OK;
    }

    while (!state->completed && processed < position_budget) {
        Ft8Candidate candidate;

        candidate.time_sub = state->time_sub;
        candidate.freq_sub = state->freq_sub;
        candidate.time_offset = state->time_offset;
        candidate.freq_offset = state->freq_offset;

        if (!prepare_score_terms(wf, state))
            return FT8_DECODER_ERR_INVALID;
        candidate.score = (int16_t)ft8_sync_score_search(state,
                                                         candidate.freq_offset);

        if (candidate.score >= state->min_score) {
            if (state->heap_size == state->capacity &&
                candidate.score > candidates[0].score) {
                --state->heap_size;
                candidates[0] = candidates[state->heap_size];
                heapify_down(candidates, state->heap_size);
            }

            if (state->heap_size < state->capacity) {
                candidates[state->heap_size] = candidate;
                ++state->heap_size;
                heapify_up(candidates, state->heap_size);
            }
        }

        candidate_search_advance(wf, state);
        ++processed;
    }

    if (state->completed) {
        candidate_search_sort(candidates, state->heap_size);
        *out_completed = 1;
    }
    *out_count = state->heap_size;
    return FT8_DECODER_OK;
}

Ft8DecoderStatus ft8_decoder_find_candidates(const Ft8WaterfallView *wf,
                                             Ft8Candidate *candidates,
                                             size_t capacity,
                                             int min_score,
                                             size_t *out_count)
{
    Ft8CandidateSearchState state;
    int completed = 0;

    if (out_count)
        *out_count = 0u;
    if (!waterfall_valid(wf) || candidates == NULL || capacity == 0u || out_count == NULL)
        return FT8_DECODER_ERR_INVALID;

    if (ft8_decoder_candidate_search_begin(&state, capacity, min_score) != FT8_DECODER_OK)
        return FT8_DECODER_ERR_INVALID;

    return ft8_decoder_candidate_search_step(wf,
                                             &state,
                                             candidates,
                                             SIZE_MAX,
                                             &completed,
                                             out_count);
}

static float wf_mag(uint8_t x)
{
    return (float)x * 0.5f - 120.0f;
}

static float max2(float a, float b)
{
    return (a >= b) ? a : b;
}

static float max4(float a, float b, float c, float d)
{
    return max2(max2(a, b), max2(c, d));
}

static void extract_symbol(const uint8_t *wf, float *logl)
{
    float s2[8];
    for (int j = 0; j < 8; ++j)
        s2[j] = wf_mag(wf[kGrayMap[j]]);

    logl[0] = max4(s2[4], s2[5], s2[6], s2[7]) - max4(s2[0], s2[1], s2[2], s2[3]);
    logl[1] = max4(s2[2], s2[3], s2[6], s2[7]) - max4(s2[0], s2[1], s2[4], s2[5]);
    logl[2] = max4(s2[1], s2[3], s2[5], s2[7]) - max4(s2[0], s2[2], s2[4], s2[6]);
}

static void extract_likelihood(const Ft8WaterfallView *wf,
                               const Ft8Candidate *candidate,
                               float log174[FT8_LDPC_N])
{
    for (int k = 0; k < FT8_DATA_SYMBOLS; ++k) {
        int symbol = k + ((k < 29) ? 7 : 14);
        int bit = 3 * k;
        const uint8_t *mag = candidate_symbol(wf, candidate, symbol);

        if (!mag) {
            log174[bit + 0] = 0;
            log174[bit + 1] = 0;
            log174[bit + 2] = 0;
        } else {
            extract_symbol(mag, log174 + bit);
        }
    }
}

static void normalize_likelihood(float log174[FT8_LDPC_N])
{
    float sum = 0;
    float sum2 = 0;
    for (int i = 0; i < FT8_LDPC_N; ++i) {
        sum += log174[i];
        sum2 += log174[i] * log174[i];
    }

    float inv_n = 1.0f / FT8_LDPC_N;
    float variance = (sum2 - (sum * sum * inv_n)) * inv_n;
    float norm_factor = sqrtf(24.0f / variance);
    for (int i = 0; i < FT8_LDPC_N; ++i)
        log174[i] *= norm_factor;
}

static void pack_bits(const uint8_t bits[], int num_bits, uint8_t packed[])
{
    int num_bytes = (num_bits + 7) / 8;
    uint8_t mask = 0x80u;
    int byte_idx = 0;

    memset(packed, 0, (size_t)num_bytes);
    for (int i = 0; i < num_bits; ++i) {
        if (bits[i])
            packed[byte_idx] |= mask;
        mask >>= 1;
        if (!mask) {
            mask = 0x80u;
            ++byte_idx;
        }
    }
}

Ft8DecoderStatus ft8_decoder_decode_candidate(const Ft8WaterfallView *wf,
                                              const Ft8Candidate *candidate,
                                              int max_iterations,
                                              Ft8DecodedPayload *out_payload)
{
    float log174[FT8_LDPC_N];
    uint8_t plain174[FT8_LDPC_N];
    uint8_t a91[FT8_LDPC_K_BYTES];

    if (!waterfall_valid(wf) || !candidate || !out_payload || max_iterations <= 0)
        return FT8_DECODER_ERR_INVALID;

    memset(out_payload, 0, sizeof(*out_payload));
    out_payload->candidate = *candidate;

    extract_likelihood(wf, candidate, log174);
    normalize_likelihood(log174);
    ft8_ldpc_decode(log174, max_iterations, plain174, &out_payload->ldpc_errors);
    if (out_payload->ldpc_errors > 0)
        return FT8_DECODER_ERR_LDPC;

    pack_bits(plain174, FT8_LDPC_K, a91);
    out_payload->crc_extracted = ft8_crc_extract(a91);

    a91[9] &= 0xF8u;
    a91[10] = 0;
    out_payload->crc_calculated = ft8_crc_compute(a91, 96 - 14);
    if (out_payload->crc_extracted != out_payload->crc_calculated)
        return FT8_DECODER_ERR_CRC;

    memcpy(out_payload->payload, a91, FT8_PAYLOAD_BYTES);
    return FT8_DECODER_OK;
}

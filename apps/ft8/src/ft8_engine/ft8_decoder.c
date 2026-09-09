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
    if (!wf || !wf->mag || wf->num_blocks > wf->max_blocks ||
        wf->num_bins < 8u || wf->time_osr == 0u || wf->freq_osr == 0u)
        return 0;
    return wf->block_stride == wf->time_osr * wf->freq_osr * wf->num_bins;
}

static const uint8_t *candidate_symbol(const Ft8WaterfallView *wf,
                                       const Ft8Candidate *candidate,
                                       int symbol_index)
{
    int block = candidate->time_offset + symbol_index;
    size_t offset;

    if (block < 0 || block >= (int)wf->num_blocks)
        return NULL;
    if (candidate->time_sub >= wf->time_osr || candidate->freq_sub >= wf->freq_osr ||
        candidate->freq_offset < 0 || candidate->freq_offset + 7 >= (int)wf->num_bins)
        return NULL;

    offset = (size_t)block * wf->block_stride;
    offset += (size_t)candidate->time_sub * wf->freq_osr * wf->num_bins;
    offset += (size_t)candidate->freq_sub * wf->num_bins;
    offset += (size_t)candidate->freq_offset;
    return wf->mag + offset;
}

static int ft8_sync_score(const Ft8WaterfallView *wf, const Ft8Candidate *candidate)
{
    int score = 0;
    int num_average = 0;

    for (int m = 0; m < FT8_NUM_SYNC; ++m) {
        for (int k = 0; k < FT8_LENGTH_SYNC; ++k) {
            int symbol = FT8_SYNC_OFFSET * m + k;
            int block_abs = candidate->time_offset + symbol;
            const uint8_t *p8;
            int sm;

            if (block_abs < 0)
                continue;
            if (block_abs >= (int)wf->num_blocks)
                break;

            p8 = candidate_symbol(wf, candidate, symbol);
            if (!p8)
                continue;

            sm = kCostasPattern[k];
            if (sm > 0) {
                score += (int)p8[sm] - (int)p8[sm - 1];
                ++num_average;
            }
            if (sm < 7) {
                score += (int)p8[sm] - (int)p8[sm + 1];
                ++num_average;
            }
            if ((k > 0) && (block_abs > 0)) {
                const uint8_t *prev = candidate_symbol(wf, candidate, symbol - 1);
                if (prev) {
                    score += (int)p8[sm] - (int)prev[sm];
                    ++num_average;
                }
            }
            if (((k + 1) < FT8_LENGTH_SYNC) && ((block_abs + 1) < (int)wf->num_blocks)) {
                const uint8_t *next = candidate_symbol(wf, candidate, symbol + 1);
                if (next) {
                    score += (int)p8[sm] - (int)next[sm];
                    ++num_average;
                }
            }
        }
    }

    if (num_average > 0)
        score /= num_average;
    return score;
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

Ft8DecoderStatus ft8_decoder_find_candidates(const Ft8WaterfallView *wf,
                                             Ft8Candidate *candidates,
                                             size_t capacity,
                                             int min_score,
                                             size_t *out_count)
{
    size_t heap_size = 0u;
    Ft8Candidate candidate;

    if (out_count)
        *out_count = 0u;
    if (!waterfall_valid(wf) || !candidates || capacity == 0u || !out_count)
        return FT8_DECODER_ERR_INVALID;

    for (candidate.time_sub = 0u; candidate.time_sub < wf->time_osr; ++candidate.time_sub) {
        for (candidate.freq_sub = 0u; candidate.freq_sub < wf->freq_osr; ++candidate.freq_sub) {
            for (candidate.time_offset = -10; candidate.time_offset < 20; ++candidate.time_offset) {
                for (candidate.freq_offset = 0;
                     candidate.freq_offset + 7 < (int)wf->num_bins;
                     ++candidate.freq_offset) {
                    candidate.score = (int16_t)ft8_sync_score(wf, &candidate);
                    if (candidate.score < min_score)
                        continue;

                    if (heap_size == capacity && candidate.score > candidates[0].score) {
                        --heap_size;
                        candidates[0] = candidates[heap_size];
                        heapify_down(candidates, heap_size);
                    }

                    if (heap_size < capacity) {
                        candidates[heap_size] = candidate;
                        ++heap_size;
                        heapify_up(candidates, heap_size);
                    }
                }
            }
        }
    }

    for (size_t len_unsorted = heap_size; len_unsorted > 1u; --len_unsorted) {
        Ft8Candidate tmp = candidates[len_unsorted - 1u];
        candidates[len_unsorted - 1u] = candidates[0];
        candidates[0] = tmp;
        heapify_down(candidates, len_unsorted - 1u);
    }

    *out_count = heap_size;
    return FT8_DECODER_OK;
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

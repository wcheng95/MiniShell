#include "js8_decoder.h"

#include "js8_crc.h"
#include "js8_ldpc.h"

#include <math.h>
#include <string.h>

#define JS8_DATA_SYMBOLS 58
#define JS8_LENGTH_SYNC 7
#define JS8_NUM_SYNC 3
#define JS8_SYNC_OFFSET 36

static const uint8_t kCostasPattern[7] = { 4, 2, 5, 6, 1, 3, 0 };

static int waterfall_valid(const Js8WaterfallView *wf)
{
    if (!wf || !wf->mag || wf->max_blocks == 0u ||
        wf->max_blocks > JS8_MONITOR_LINEAR_BLOCKS ||
        wf->num_blocks > wf->max_blocks || wf->num_bins < 8u ||
        wf->num_bins > JS8_MONITOR_BLOCK_SIZE / 2u + 1u ||
        wf->time_osr == 0u || wf->time_osr > UINT8_MAX ||
        wf->freq_osr == 0u || wf->freq_osr > UINT8_MAX ||
        (uint64_t)wf->max_blocks * wf->block_stride > PTRDIFF_MAX)
        return 0;
    return wf->block_stride == wf->time_osr * wf->freq_osr * wf->num_bins;
}

static int candidate_valid(const Js8WaterfallView *wf, const Js8Candidate *candidate)
{
    return candidate && candidate->time_sub < wf->time_osr &&
           candidate->freq_sub < wf->freq_osr && candidate->freq_offset >= 0 &&
           candidate->freq_offset + 7 < (int)wf->num_bins &&
           (int64_t)candidate->time_offset + 78 >= wf->first_block &&
           candidate->time_offset < (int64_t)wf->first_block + wf->num_blocks;
}

static const uint8_t *candidate_symbol(const Js8WaterfallView *wf,
                                       const Js8Candidate *candidate,
                                       int symbol_index)
{
    int logical;
    int64_t last_block;
    int64_t offset;

    if (!waterfall_valid(wf) || !candidate ||
        candidate->time_sub >= wf->time_osr ||
        candidate->freq_sub >= wf->freq_osr ||
        candidate->freq_offset < 0 ||
        candidate->freq_offset + 7 >= (int)wf->num_bins)
        return NULL;

    logical = candidate->time_offset + symbol_index;
    last_block = (int64_t)wf->first_block + (int64_t)wf->num_blocks;
    if ((int64_t)logical < (int64_t)wf->first_block ||
        (int64_t)logical >= last_block)
        return NULL;

    offset = ((int64_t)logical - wf->first_block) * (int64_t)wf->block_stride;
    offset += (int64_t)candidate->time_sub *
              (int64_t)wf->freq_osr * (int64_t)wf->num_bins;
    offset += (int64_t)candidate->freq_sub * (int64_t)wf->num_bins;
    offset += (int64_t)candidate->freq_offset;
    return wf->mag + offset;
}

static int js8_sync_score_direct(const Js8WaterfallView *wf,
                                 const Js8Candidate *candidate)
{
    int score = 0;
    int num_average = 0;
    int64_t last_block;
    int64_t base_offset;
    const uint8_t *mag_cand;

    last_block = (int64_t)wf->first_block + (int64_t)wf->num_blocks;
    if ((int64_t)candidate->time_offset < (int64_t)wf->first_block ||
        (int64_t)candidate->time_offset >= last_block)
        return 0;

    base_offset = ((int64_t)candidate->time_offset - wf->first_block) *
                  (int64_t)wf->block_stride;
    base_offset += (int64_t)candidate->time_sub *
                   (int64_t)wf->freq_osr * (int64_t)wf->num_bins;
    base_offset += (int64_t)candidate->freq_sub * (int64_t)wf->num_bins;
    base_offset += (int64_t)candidate->freq_offset;
    mag_cand = wf->mag + base_offset;

    for (int m = 0; m < JS8_NUM_SYNC; ++m) {
        for (int k = 0; k < JS8_LENGTH_SYNC; ++k) {
            int block = JS8_SYNC_OFFSET * m + k;
            int logical = candidate->time_offset + block;
            const uint8_t *p8;
            int sm;

            if ((int64_t)logical < (int64_t)wf->first_block)
                continue;
            if ((int64_t)logical >= last_block)
                break;

            p8 = mag_cand + (ptrdiff_t)block * wf->block_stride;
            sm = kCostasPattern[k];

            if (sm > 0) {
                score += (int)p8[sm] - (int)p8[sm - 1];
                ++num_average;
            }
            if (sm < 7) {
                score += (int)p8[sm] - (int)p8[sm + 1];
                ++num_average;
            }
            if (k > 0 && logical > wf->first_block) {
                score += (int)p8[sm] -
                         (int)p8[sm - (ptrdiff_t)wf->block_stride];
                ++num_average;
            }
            if ((k + 1) < JS8_LENGTH_SYNC &&
                (int64_t)(logical + 1) < last_block) {
                score += (int)p8[sm] -
                         (int)p8[sm + (ptrdiff_t)wf->block_stride];
                ++num_average;
            }
        }
    }

    return num_average > 0 ? score / num_average : 0;
}

static void heapify_down(Js8Candidate heap[], size_t heap_size)
{
    size_t current = 0u;
    for (;;) {
        size_t left = 2u * current + 1u;
        size_t right = left + 1u;
        size_t smallest = current;
        Js8Candidate tmp;

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

static void heapify_up(Js8Candidate heap[], size_t heap_size)
{
    size_t current = heap_size - 1u;
    while (current > 0u) {
        size_t parent = (current - 1u) / 2u;
        Js8Candidate tmp;
        if (!(heap[current].score < heap[parent].score))
            break;
        tmp = heap[parent];
        heap[parent] = heap[current];
        heap[current] = tmp;
        current = parent;
    }
}

static void candidate_search_sort(Js8Candidate candidates[], size_t heap_size)
{
    for (size_t len_unsorted = heap_size; len_unsorted > 1u; --len_unsorted) {
        Js8Candidate tmp = candidates[len_unsorted - 1u];
        candidates[len_unsorted - 1u] = candidates[0];
        candidates[0] = tmp;
        heapify_down(candidates, len_unsorted - 1u);
    }
}

Js8DecoderStatus js8_decoder_find_candidates(const Js8WaterfallView *wf,
                                             Js8Candidate *candidates,
                                             size_t capacity,
                                             int min_score,
                                             size_t *out_count)
{
    size_t heap_size = 0u;
    Js8Candidate candidate;

    if (out_count)
        *out_count = 0u;
    if (!waterfall_valid(wf) || candidates == NULL || capacity == 0u ||
        capacity > JS8_DECODER_CANDIDATE_CAPACITY ||
        out_count == NULL)
        return JS8_DECODER_ERR_INVALID;

    for (candidate.time_sub = 0u;
         candidate.time_sub < wf->time_osr;
         ++candidate.time_sub) {
        for (candidate.freq_sub = 0u;
             candidate.freq_sub < wf->freq_osr;
             ++candidate.freq_sub) {
            for (candidate.time_offset = -10;
                 candidate.time_offset < 20;
                 ++candidate.time_offset) {
                for (candidate.freq_offset = 0;
                     candidate.freq_offset + 7 < (int)wf->num_bins;
                     ++candidate.freq_offset) {
                    candidate.score =
                        (int16_t)js8_sync_score_direct(wf, &candidate);

                    if (candidate.score < min_score)
                        continue;

                    if (heap_size == capacity &&
                        candidate.score > candidates[0].score) {
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

    candidate_search_sort(candidates, heap_size);
    *out_count = heap_size;
    return JS8_DECODER_OK;
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
        s2[j] = wf_mag(wf[j]);

    logl[0] = max4(s2[4], s2[5], s2[6], s2[7]) - max4(s2[0], s2[1], s2[2], s2[3]);
    logl[1] = max4(s2[2], s2[3], s2[6], s2[7]) - max4(s2[0], s2[1], s2[4], s2[5]);
    logl[2] = max4(s2[1], s2[3], s2[5], s2[7]) - max4(s2[0], s2[2], s2[4], s2[6]);
}

static void extract_likelihood(const Js8WaterfallView *wf,
                               const Js8Candidate *candidate,
                               float log174[JS8_CODEWORD_BITS])
{
    for (int k = 0; k < JS8_DATA_SYMBOLS; ++k) {
        int symbol = k + ((k < 29) ? 7 : 14);
        int bit = 3 * k;
        const uint8_t *mag = NULL;

        /* Missing rows are erasures; no UTC/slot overwrite policy here. */
        mag = candidate_symbol(wf, candidate, symbol);

        if (!mag) {
            log174[bit + 0] = 0;
            log174[bit + 1] = 0;
            log174[bit + 2] = 0;
        } else {
            extract_symbol(mag, log174 + bit);
        }
    }
}

static int normalize_likelihood(float log174[JS8_CODEWORD_BITS])
{
    float sum = 0;
    float sum2 = 0;
    for (unsigned i = 0; i < JS8_CODEWORD_BITS; ++i) {
        sum += log174[i];
        sum2 += log174[i] * log174[i];
    }

    float inv_n = 1.0f / JS8_CODEWORD_BITS;
    float variance = (sum2 - (sum * sum * inv_n)) * inv_n;
    /* A flat waterfall has no evidence; avoid 0 * infinity / NaN LLRs. */
    if (!(variance > 0.0f) || !isfinite(variance))
        return 0;
    float norm_factor = sqrtf(24.0f / variance);
    for (unsigned i = 0; i < JS8_CODEWORD_BITS; ++i)
        log174[i] *= norm_factor;
    return 1;
}

Js8DecoderStatus js8_decoder_extract_likelihood(const Js8WaterfallView *wf,
                                               const Js8Candidate *candidate,
                                               float llr[JS8_CODEWORD_BITS])
{
    if (!waterfall_valid(wf) || !candidate_valid(wf, candidate) || !llr)
        return JS8_DECODER_ERR_INVALID;
    extract_likelihood(wf, candidate, llr);
    return JS8_DECODER_OK;
}

Js8DecoderStatus js8_decoder_decode_candidate(const Js8WaterfallView *wf,
                                              const Js8Candidate *candidate,
                                              Js8DecodedPayload *out_payload)
{
    float llr[JS8_CODEWORD_BITS];
    uint8_t codeword[JS8_CODEWORD_BITS];
    uint8_t info[JS8_INFO_BITS];

    if (!out_payload || js8_decoder_extract_likelihood(wf, candidate, llr) != JS8_DECODER_OK)
        return JS8_DECODER_ERR_INVALID;

    memset(out_payload, 0, sizeof(*out_payload));
    out_payload->candidate = *candidate;
    out_payload->ldpc_errors = -1;
    if (!normalize_likelihood(llr) ||
        js8_ldpc_decode(llr, info, codeword, &out_payload->ldpc_errors) != 0)
        return JS8_DECODER_ERR_LDPC;
    if (js8_crc12_check(info) != 1)
        return JS8_DECODER_ERR_CRC;
    memcpy(out_payload->payload_bits, info, JS8_PAYLOAD_BITS);
    return JS8_DECODER_OK;
}

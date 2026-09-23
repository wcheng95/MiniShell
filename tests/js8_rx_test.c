#include "js8_monitor.h"
#include "js8_decoder.h"
#include "js8_channel.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct golden {
    const char *payload;
    unsigned crc;
    const char *info;
    const char *codeword;
    uint8_t tones[JS8_TONE_COUNT];
};
#include "js8_golden_vectors.h"

static void *workspace(const Js8MonitorRequirements *req)
{
    void *p = aligned_alloc(req->alignment, req->total_bytes);
    assert(p);
    return p;
}

static void assert_zero(const void *data, size_t length)
{
    const unsigned char *p = data;
    for (size_t i = 0; i < length; ++i) assert(p[i] == 0);
}

static void monitor_tests(void)
{
    Js8MonitorConfig cfg = js8_monitor_baseline_config();
    Js8MonitorRequirements req;
    assert(cfg.time_osr == 2 && cfg.freq_osr == 2);
    assert(js8_monitor_query_requirements(&cfg, &req) == JS8_MONITOR_OK);
    assert(req.block_size == 960 && req.subblock_size == 480 && req.nfft == 1920);
    assert(req.min_bin == 32 && req.max_bin == 465 && req.num_bins == 433);
    assert(req.max_blocks == 93 && req.block_stride == 1732);
    assert(req.waterfall_bytes == 161076);
    assert(req.window_bytes == 7680 && req.history_bytes == 7680);
    assert(req.time_scratch_bytes == 7680 && req.freq_scratch_bytes == 7688);
    assert(req.total_bytes % req.alignment == 0);
    printf("JS8 resources: total=%zu waterfall=%zu plan=%zu window=%zu history=%zu "
           "time_scratch=%zu freq_scratch=%zu alignment=%zu stride=%u bins=%u "
           "sizeof_monitor=%zu candidate=%zu candidate_list=%zu decoded=%zu\n",
           req.total_bytes, req.waterfall_bytes, req.fft_plan_bytes,
           req.window_bytes, req.history_bytes, req.time_scratch_bytes,
           req.freq_scratch_bytes, req.alignment, req.block_stride, req.num_bins,
           sizeof(Js8Monitor), sizeof(Js8Candidate),
           sizeof(Js8Candidate) * JS8_DECODER_CANDIDATE_CAPACITY, sizeof(Js8DecodedPayload));

    Js8Monitor a, b;
    void *ma = workspace(&req), *mb = workspace(&req);
    assert(js8_monitor_init(NULL, &cfg, ma, req.total_bytes) == JS8_MONITOR_ERR_INVALID);
    assert(js8_monitor_init(&a, &cfg, NULL, req.total_bytes) == JS8_MONITOR_ERR_INVALID);
    assert(js8_monitor_init(&a, &cfg, ma, req.total_bytes - 1) == JS8_MONITOR_ERR_WORKSPACE);
    assert(!a.initialized);
    js8_monitor_destroy(&a);
    assert(js8_monitor_init(&a, &cfg, (uint8_t *)ma + 1, req.total_bytes) == JS8_MONITOR_ERR_WORKSPACE);
    assert(js8_monitor_init(&a, &cfg, ma, req.total_bytes) == JS8_MONITOR_OK);
    assert(js8_monitor_init(&b, &cfg, mb, req.total_bytes) == JS8_MONITOR_OK);
    float block[960];
    for (unsigned i = 0; i < 960; ++i)
        block[i] = (float)(0.25 * sin(2.0 * 3.14159265358979323846 * 1000 * i / 6000));
    for (unsigned i = 0; i < 4; ++i) {
        assert(js8_monitor_process_block(&a, block) == JS8_MONITOR_OK);
        assert(js8_monitor_process_block(&b, block) == JS8_MONITOR_OK);
    }
    assert(a.fft_cfg != b.fft_cfg && a.history != b.history);
    assert(memcmp(a.waterfall, b.waterfall, req.waterfall_bytes) == 0);
    assert(memcmp(a.history, b.history, req.history_bytes) == 0);
    assert(js8_monitor_process_block(&a, NULL) == JS8_MONITOR_ERR_INVALID);
    block[959] = NAN;
    assert(js8_monitor_process_block(&a, block) == JS8_MONITOR_ERR_INVALID);
    block[959] = 2;
    assert(js8_monitor_process_block(&a, block) == JS8_MONITOR_ERR_INVALID);
    assert(a.num_blocks == 4 && memcmp(a.history, b.history, req.history_bytes) == 0);
    js8_monitor_reset_window(&a);
    assert(a.num_blocks == 0 && a.max_mag_db == -120.0f);
    assert_zero(a.history, req.history_bytes);
    assert(memcmp(a.waterfall, b.waterfall, req.waterfall_bytes) == 0);
    assert(b.num_blocks == 4);
    js8_monitor_reset_stream(&a);
    assert_zero(a.waterfall, req.waterfall_bytes);
    memset(block, 0, sizeof(block));
    for (unsigned i = 0; i < 93; ++i)
        assert(js8_monitor_process_block(&a, block) == JS8_MONITOR_OK);
    assert(js8_monitor_process_block(&a, block) == JS8_MONITOR_WATERFALL_FULL);
    assert(a.num_blocks == 93);
    Js8WaterfallView view;
    assert(js8_monitor_get_waterfall(&a, &view) == JS8_MONITOR_OK);
    assert(view.mag == a.waterfall && view.num_blocks == 93 && view.first_block == 0);
    assert(js8_monitor_get_waterfall(&a, NULL) == JS8_MONITOR_ERR_INVALID);
    js8_monitor_destroy(&a);
    js8_monitor_destroy(&a);
    js8_monitor_destroy(NULL);
    assert(!a.initialized && !a.workspace);
    assert(js8_monitor_process_block(&a, block) == JS8_MONITOR_ERR_NOT_INITIALIZED);
    assert(js8_monitor_get_waterfall(&a, &view) == JS8_MONITOR_ERR_NOT_INITIALIZED);
    js8_monitor_reset_window(&a);
    js8_monitor_reset_stream(&a);
    js8_monitor_destroy(&b);
    free(ma); free(mb);

    cfg.freq_osr = 1;
    assert(js8_monitor_query_requirements(&cfg, &req) == JS8_MONITOR_OK);
    assert(req.nfft == 960 && req.block_stride == 866);
    cfg.freq_osr = 7; /* unsupported FFT factor: rejected before allocation */
    assert(js8_monitor_query_requirements(&cfg, &req) == JS8_MONITOR_ERR_INVALID);
    cfg = js8_monitor_baseline_config(); cfg.f_min_hz = NAN;
    assert(js8_monitor_query_requirements(&cfg, &req) == JS8_MONITOR_ERR_INVALID);
    cfg = js8_monitor_baseline_config(); cfg.f_max_hz = INFINITY;
    assert(js8_monitor_query_requirements(&cfg, &req) == JS8_MONITOR_ERR_INVALID);
    cfg = js8_monitor_baseline_config(); cfg.sample_rate_hz = 12000;
    assert(js8_monitor_query_requirements(&cfg, &req) == JS8_MONITOR_ERR_INVALID);
    cfg = js8_monitor_baseline_config(); cfg.time_osr = 7;
    assert(js8_monitor_query_requirements(&cfg, &req) == JS8_MONITOR_ERR_INVALID);
    cfg = js8_monitor_baseline_config(); cfg.freq_osr = UINT32_MAX;
    assert(js8_monitor_query_requirements(&cfg, &req) == JS8_MONITOR_ERR_INVALID);
    assert(js8_monitor_query_requirements(NULL, &req) == JS8_MONITOR_ERR_INVALID);
    assert(js8_monitor_query_requirements(&cfg, NULL) == JS8_MONITOR_ERR_INVALID);
}

/* A tiny hand-built waterfall isolates mapping and failure status from FFT. */
static Js8WaterfallView small_view(uint8_t *mag)
{
    Js8WaterfallView wf = {.mag = mag, .max_blocks = 93, .num_blocks = 93,
        .num_bins = 16, .time_osr = 1, .freq_osr = 1, .block_stride = 16};
    return wf;
}

static unsigned data_symbol(unsigned word)
{
    return word < 29 ? word + 7 : word + 14;
}

static void set_codeword(uint8_t *mag, const uint8_t cw[174])
{
    memset(mag, 20, 93 * 16);
    for (unsigned word = 0; word < 58; ++word) {
        unsigned b = word * 3;
        unsigned tone = 4u * cw[b] + 2u * cw[b + 1] + cw[b + 2];
        mag[data_symbol(word) * 16 + tone] = 220;
    }
}

static void decoder_tests(void)
{
    uint8_t mag[93 * 16];
    Js8WaterfallView wf = small_view(mag);
    Js8Candidate c = {0};
    Js8DecodedPayload decoded;
    float llr[174];
    memset(mag, 20, sizeof(mag));
    for (unsigned word = 0; word < 58; ++word)
        mag[data_symbol(word) * 16 + word % 8] = 220;
    assert(js8_decoder_extract_likelihood(&wf, &c, llr) == JS8_DECODER_OK);
    for (unsigned i = 0; i < 174; ++i)
        assert(llr[i] == (((i / 3 % 8) >> (2 - i % 3)) & 1 ? 100.0f : -100.0f));
    uint8_t lanes[93 * 64] = {0};
    Js8WaterfallView oversampled = wf;
    oversampled.mag = lanes;
    oversampled.time_osr = oversampled.freq_osr = 2;
    oversampled.block_stride = 64;
    Js8Candidate lane_candidate = {.time_sub = 1, .freq_sub = 1, .freq_offset = 8};
    for (unsigned word = 0; word < 58; ++word)
        lanes[data_symbol(word) * 64 + 48 + 8 + word % 8] = 200;
    assert(js8_decoder_extract_likelihood(&oversampled, &lane_candidate, llr) == JS8_DECODER_OK);
    for (unsigned i = 0; i < 174; ++i)
        assert(llr[i] == (((i / 3 % 8) >> (2 - i % 3)) & 1 ? 100.0f : -100.0f));
    /* Logical labels may be negative; pointers still start at allocation head. */
    wf.first_block = -10; c.time_offset = -10;
    assert(js8_decoder_extract_likelihood(&wf, &c, llr) == JS8_DECODER_OK);
    assert(llr[0] == -100 && llr[3] == -100 && llr[5] == 100);
    wf.num_blocks = 8;
    assert(js8_decoder_extract_likelihood(&wf, &c, llr) == JS8_DECODER_OK);
    for (unsigned i = 3; i < 174; ++i) assert(llr[i] == 0);
    wf = small_view(mag); c.time_offset = 0;

    uint8_t cw[174], info[87];
    for (unsigned i = 0; i < 174; ++i) cw[i] = vectors[2].codeword[i] - '0';
    set_codeword(mag, cw);
    assert(js8_decoder_decode_candidate(&wf, &c, &decoded) == JS8_DECODER_OK);
    for (unsigned i = 0; i < 75; ++i)
        assert(decoded.payload_bits[i] == (uint8_t)(vectors[2].payload[i] - '0'));
    for (unsigned i = 0; i < 87; ++i) info[i] = vectors[2].info[i] - '0';
    info[75] ^= 1;
    assert(js8_ldpc_encode(info, cw) == 0);
    set_codeword(mag, cw);
    assert(js8_decoder_decode_candidate(&wf, &c, &decoded) == JS8_DECODER_ERR_CRC);
    assert_zero(decoded.payload_bits, sizeof(decoded.payload_bits));
    memset(mag, 0, sizeof(mag));
    assert(js8_decoder_decode_candidate(&wf, &c, &decoded) == JS8_DECODER_ERR_LDPC);
    assert(decoded.ldpc_errors == -1);
    assert_zero(decoded.payload_bits, sizeof(decoded.payload_bits));
    uint32_t state = 0x12345678u;
    for (unsigned i = 0; i < 174; ++i) {
        state = state * 1664525u + 1013904223u; cw[i] = state >> 31;
    }
    set_codeword(mag, cw);
    assert(js8_decoder_decode_candidate(&wf, &c, &decoded) == JS8_DECODER_ERR_LDPC);

    struct { Js8Candidate values[3]; uint32_t guard; } candidates = {.guard = 0x1234};
    size_t count = 999;
    memset(mag, 0, sizeof(mag));
    assert(js8_decoder_find_candidates(&wf, candidates.values, 3, 1, &count) == JS8_DECODER_OK);
    assert(count == 0);
    assert(js8_decoder_find_candidates(&wf, candidates.values, 3, 0, &count) == JS8_DECODER_OK);
    assert(count == 3 && candidates.guard == 0x1234);
    assert(js8_decoder_find_candidates(&wf, candidates.values, 0, 0, &count) == JS8_DECODER_ERR_INVALID);
    assert(js8_decoder_find_candidates(&wf, candidates.values, 51, 0, &count) == JS8_DECODER_ERR_INVALID);
    assert(js8_decoder_find_candidates(NULL, candidates.values, 3, 0, &count) == JS8_DECODER_ERR_INVALID);
    assert(count == 0);
    assert(js8_decoder_find_candidates(&wf, NULL, 3, 0, &count) == JS8_DECODER_ERR_INVALID);
    assert(js8_decoder_find_candidates(&wf, candidates.values, 3, 0, NULL) == JS8_DECODER_ERR_INVALID);
    assert(js8_decoder_decode_candidate(NULL, &c, &decoded) == JS8_DECODER_ERR_INVALID);
    assert(js8_decoder_decode_candidate(&wf, NULL, &decoded) == JS8_DECODER_ERR_INVALID);
    assert(js8_decoder_decode_candidate(&wf, &c, NULL) == JS8_DECODER_ERR_INVALID);
    assert(js8_decoder_extract_likelihood(&wf, &c, NULL) == JS8_DECODER_ERR_INVALID);
    for (unsigned i = 0; i < 5; ++i) {
        Js8Candidate bad = c;
        if (i == 0) bad.freq_offset = -1;
        if (i == 1) bad.freq_offset = 9;
        if (i == 2) bad.time_sub = 1;
        if (i == 3) bad.freq_sub = 1;
        if (i == 4) bad.time_offset = 93;
        memset(&decoded, 0xa5, sizeof(decoded));
        assert(js8_decoder_decode_candidate(&wf, &bad, &decoded) == JS8_DECODER_ERR_INVALID);
        const unsigned char *bytes = (const unsigned char *)&decoded;
        for (size_t j = 0; j < sizeof(decoded); ++j) assert(bytes[j] == 0xa5);
    }
    for (unsigned i = 0; i < 6; ++i) {
        Js8WaterfallView bad = wf;
        if (i == 0) bad.mag = NULL;
        if (i == 1) bad.block_stride = 15;
        if (i == 2) bad.num_blocks = 94;
        if (i == 3) bad.time_osr = 256;
        if (i == 4) bad.num_bins = UINT32_MAX;
        if (i == 5) bad.freq_osr = 0;
        assert(js8_decoder_find_candidates(&bad, candidates.values, 3, 0, &count) == JS8_DECODER_ERR_INVALID);
        assert(js8_decoder_decode_candidate(&bad, &c, &decoded) == JS8_DECODER_ERR_INVALID);
    }
    /* Isolate each group: all three positions must contribute identical score. */
    const uint8_t costas[7] = {4, 2, 5, 6, 1, 3, 0};
    int score = -1;
    for (unsigned group = 0; group < 3; ++group) {
        memset(mag, 0, sizeof(mag));
        for (unsigned k = 0; k < 7; ++k) mag[(36 * group + k) * 16 + costas[k]] = 200;
        Js8Candidate list[50];
        assert(js8_decoder_find_candidates(&wf, list, 50, 5, &count) == JS8_DECODER_OK);
        int found = 0;
        for (size_t i = 0; i < count; ++i) {
            if (i) assert(list[i-1].score >= list[i].score);
            if (list[i].time_offset == 0 && list[i].freq_offset == 0) {
                if (!group) score = list[i].score;
                assert(list[i].score == score && score > 0); found = 1;
            }
        }
        assert(found);
    }
}

static void synthetic_rx(void)
{
    Js8MonitorConfig cfg = js8_monitor_baseline_config();
    Js8MonitorRequirements req;
    assert(js8_monitor_query_requirements(&cfg, &req) == JS8_MONITOR_OK);
    void *mem = workspace(&req);
    Js8Monitor monitor;
    assert(js8_monitor_init(&monitor, &cfg, mem, req.total_bytes) == JS8_MONITOR_OK);
    double phase = 0;
    const double tau = 6.28318530717958647692;
    for (unsigned block = 0; block < 93; ++block) {
        float samples[960];
        for (unsigned i = 0; i < 960; ++i) {
            unsigned n = block * 960 + i;
            samples[i] = 0;
            if (n >= 3000 && n < 3000 + 79 * 960) {
                unsigned symbol = (n - 3000) / 960;
                /* Pinned upstream tones, never js8_channel_encode(). */
                double frequency = 1000.0 + 6.25 * vectors[2].tones[symbol];
                samples[i] = (float)(0.5 * sin(phase));
                phase += tau * frequency / 6000.0;
                if (phase >= tau) phase -= tau;
            }
        }
        assert(js8_monitor_process_block(&monitor, samples) == JS8_MONITOR_OK);
    }
    Js8WaterfallView wf;
    assert(js8_monitor_get_waterfall(&monitor, &wf) == JS8_MONITOR_OK);
    Js8Candidate candidates[50];
    size_t count;
    assert(js8_decoder_find_candidates(&wf, candidates, 50, 5, &count) == JS8_DECODER_OK);
    assert(count > 0 && count <= 50);
    int found = 0;
    for (size_t i = 0; i < count; ++i) {
        Js8DecodedPayload out;
        if (i) assert(candidates[i-1].score >= candidates[i].score);
        if (js8_decoder_decode_candidate(&wf, &candidates[i], &out) != JS8_DECODER_OK) continue;
        int same = 1;
        for (unsigned j = 0; j < 75; ++j)
            if (out.payload_bits[j] != (uint8_t)(vectors[2].payload[j] - '0')) same = 0;
        if (same) {
            uint8_t info[87];
            assert(js8_crc12_append(out.payload_bits, info) == 0 && js8_crc12_check(info) == 1);
            printf("JS8 synthetic: payload exact, candidates=%zu rank=%zu score=%d "
                   "time=%d/%u freq=%d/%u hard_errors=%d\n", count, i,
                   out.candidate.score, out.candidate.time_offset, out.candidate.time_sub,
                   out.candidate.freq_offset, out.candidate.freq_sub, out.ldpc_errors);
            found = 1; break;
        }
    }
    assert(found);
    js8_monitor_destroy(&monitor);
    free(mem);
}

int main(void)
{
    monitor_tests();
    decoder_tests();
    synthetic_rx();
    puts("js8_rx_test: PASS");
    return 0;
}

#include "js8_monitor.h"

#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "kiss_fftr.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define JS8_SYMBOL_PERIOD_SEC 0.160f

static size_t align_up_size(size_t value, size_t alignment)
{
    return (value + alignment - 1u) & ~(alignment - 1u);
}

static uintptr_t align_up_ptr(uintptr_t value, size_t alignment)
{
    return (value + alignment - 1u) & ~((uintptr_t)alignment - 1u);
}

static int valid_power_of_two_alignment(size_t alignment)
{
    return alignment != 0u && (alignment & (alignment - 1u)) == 0u;
}

static Js8MonitorStatus derive_requirements(const Js8MonitorConfig *config,
                                            Js8MonitorRequirements *out_req)
{
    size_t fft_plan_bytes = 0u;
    size_t cursor = 0u;
    const size_t alignment = _Alignof(max_align_t);
    uint32_t block_size;
    uint32_t subblock_size;
    uint32_t nfft;
    uint32_t min_bin;
    uint32_t max_bin;
    uint32_t num_bins;
    uint32_t max_blocks;
    uint32_t block_stride;
    size_t waterfall_bytes;

    if (!config || !out_req || !valid_power_of_two_alignment(alignment))
        return JS8_MONITOR_ERR_INVALID;

    if (config->sample_rate_hz != JS8_MONITOR_SAMPLE_RATE_HZ ||
        config->time_osr == 0u || config->time_osr > UINT8_MAX ||
        config->freq_osr == 0u || config->freq_osr > UINT8_MAX ||
        !isfinite(config->f_min_hz) || !isfinite(config->f_max_hz) ||
        config->f_min_hz < 0.0f || config->f_max_hz <= config->f_min_hz ||
        config->f_max_hz >= (float)config->sample_rate_hz * 0.5f)
        return JS8_MONITOR_ERR_INVALID;

    block_size = (uint32_t)((float)config->sample_rate_hz * JS8_SYMBOL_PERIOD_SEC);
    if (block_size == 0u || block_size % config->time_osr != 0u)
        return JS8_MONITOR_ERR_INVALID;

    subblock_size = block_size / config->time_osr;
    nfft = block_size * config->freq_osr;
    min_bin = (uint32_t)(config->f_min_hz * JS8_SYMBOL_PERIOD_SEC);
    max_bin = (uint32_t)(config->f_max_hz * JS8_SYMBOL_PERIOD_SEC) + 1u;
    if (max_bin <= min_bin)
        return JS8_MONITOR_ERR_INVALID;

    num_bins = max_bin - min_bin;
    block_stride = config->time_osr * config->freq_osr * num_bins;
    if (block_stride == 0u)
        return JS8_MONITOR_ERR_INVALID;

    max_blocks = JS8_MONITOR_LINEAR_BLOCKS;
    if ((uint64_t)max_blocks * block_stride > PTRDIFF_MAX)
        return JS8_MONITOR_ERR_INVALID;
    waterfall_bytes = (size_t)max_blocks * block_stride;

    if ((max_bin * config->freq_osr) > (nfft / 2u + 1u))
        return JS8_MONITOR_ERR_INVALID;

    kiss_fftr_alloc((int)nfft, 0, NULL, &fft_plan_bytes);
    if (fft_plan_bytes == 0u)
        return JS8_MONITOR_ERR_INVALID;

    memset(out_req, 0, sizeof(*out_req));
    out_req->alignment = alignment;
    out_req->fft_plan_bytes = fft_plan_bytes;
    out_req->waterfall_bytes = waterfall_bytes;
    out_req->window_bytes = (size_t)nfft * sizeof(float);
    out_req->history_bytes = (size_t)nfft * sizeof(float);
    out_req->time_scratch_bytes = (size_t)nfft * sizeof(kiss_fft_scalar);
    out_req->freq_scratch_bytes = (size_t)(nfft / 2u + 1u) * sizeof(kiss_fft_cpx);
    out_req->block_size = block_size;
    out_req->subblock_size = subblock_size;
    out_req->nfft = nfft;
    out_req->min_bin = min_bin;
    out_req->max_bin = max_bin;
    out_req->num_bins = num_bins;
    out_req->max_blocks = max_blocks;
    out_req->block_stride = block_stride;

#define ADD_SLICE(bytes_) do { \
        cursor = align_up_size(cursor, alignment); \
        cursor += (bytes_); \
    } while (0)
    ADD_SLICE(out_req->fft_plan_bytes);
    ADD_SLICE(out_req->waterfall_bytes);
    ADD_SLICE(out_req->window_bytes);
    ADD_SLICE(out_req->history_bytes);
    ADD_SLICE(out_req->time_scratch_bytes);
    ADD_SLICE(out_req->freq_scratch_bytes);
#undef ADD_SLICE

    out_req->total_bytes = align_up_size(cursor, alignment);
    return JS8_MONITOR_OK;
}

static uint8_t *take_slice(uint8_t **cursor, size_t *remaining,
                           size_t bytes, size_t alignment)
{
    uintptr_t p;
    uintptr_t aligned;
    size_t padding;

    if (!cursor || !*cursor || !remaining)
        return NULL;

    p = (uintptr_t)*cursor;
    aligned = align_up_ptr(p, alignment);
    padding = (size_t)(aligned - p);
    if (padding > *remaining || bytes > (*remaining - padding))
        return NULL;

    *cursor = (uint8_t *)(aligned + bytes);
    *remaining -= padding + bytes;
    return (uint8_t *)aligned;
}

Js8MonitorConfig js8_monitor_baseline_config(void)
{
    Js8MonitorConfig config;
    config.sample_rate_hz = JS8_MONITOR_SAMPLE_RATE_HZ;
    config.f_min_hz = JS8_MONITOR_BASELINE_F_MIN_HZ;
    config.f_max_hz = JS8_MONITOR_BASELINE_F_MAX_HZ;
    config.time_osr = JS8_MONITOR_BASELINE_TIME_OSR;
    config.freq_osr = JS8_MONITOR_BASELINE_FREQ_OSR;
    return config;
}

Js8MonitorStatus js8_monitor_query_requirements(const Js8MonitorConfig *config,
                                                Js8MonitorRequirements *out_req)
{
    return derive_requirements(config, out_req);
}

Js8MonitorStatus js8_monitor_init(Js8Monitor *monitor,
                                  const Js8MonitorConfig *config,
                                  void *workspace,
                                  size_t workspace_bytes)
{
    Js8MonitorRequirements req;
    uint8_t *cursor;
    size_t remaining;
    size_t fft_bytes;
    uint32_t i;

    if (!monitor)
        return JS8_MONITOR_ERR_INVALID;
    memset(monitor, 0, sizeof(*monitor));

    if (derive_requirements(config, &req) != JS8_MONITOR_OK || !workspace)
        return JS8_MONITOR_ERR_INVALID;
    if (((uintptr_t)workspace % req.alignment) != 0u || workspace_bytes < req.total_bytes)
        return JS8_MONITOR_ERR_WORKSPACE;

    cursor = (uint8_t *)workspace;
    remaining = workspace_bytes;

    monitor->workspace = workspace;
    monitor->workspace_bytes = workspace_bytes;
    monitor->config = *config;
    monitor->req = req;

    monitor->fft_cfg = take_slice(&cursor, &remaining, req.fft_plan_bytes, req.alignment);
    monitor->waterfall = take_slice(&cursor, &remaining, req.waterfall_bytes, req.alignment);
    monitor->window = (float *)take_slice(&cursor, &remaining, req.window_bytes, req.alignment);
    monitor->history = (float *)take_slice(&cursor, &remaining, req.history_bytes, req.alignment);
    monitor->time_scratch = (float *)take_slice(&cursor, &remaining, req.time_scratch_bytes, req.alignment);
    monitor->freq_scratch = take_slice(&cursor, &remaining, req.freq_scratch_bytes, req.alignment);

    if (!monitor->fft_cfg || !monitor->waterfall || !monitor->window || !monitor->history ||
        !monitor->time_scratch || !monitor->freq_scratch) {
        js8_monitor_destroy(monitor);
        return JS8_MONITOR_ERR_WORKSPACE;
    }

    fft_bytes = req.fft_plan_bytes;
    monitor->fft_cfg = kiss_fftr_alloc((int)req.nfft, 0, monitor->fft_cfg, &fft_bytes);
    if (!monitor->fft_cfg) {
        js8_monitor_destroy(monitor);
        return JS8_MONITOR_ERR_WORKSPACE;
    }

    monitor->fft_norm = 2.0f / (float)req.nfft;
    for (i = 0u; i < req.nfft; ++i) {
        float x = sinf((float)M_PI * (float)i / (float)req.nfft);
        float hann = x * x;
        monitor->window[i] = monitor->fft_norm * hann;
    }

    memset(monitor->history, 0, req.history_bytes);
    memset(monitor->waterfall, 0, req.waterfall_bytes);
    monitor->max_mag_db = -120.0f;
    monitor->num_blocks = 0u;
    monitor->initialized = 1;
    return JS8_MONITOR_OK;
}

void js8_monitor_destroy(Js8Monitor *monitor)
{
    if (!monitor)
        return;
    memset(monitor, 0, sizeof(*monitor));
}

void js8_monitor_reset_window(Js8Monitor *monitor)
{
    if (!monitor || !monitor->initialized)
        return;

    monitor->num_blocks = 0u;
    monitor->max_mag_db = -120.0f;
    memset(monitor->history, 0, monitor->req.history_bytes);
}

void js8_monitor_reset_stream(Js8Monitor *monitor)
{
    if (!monitor || !monitor->initialized)
        return;

    js8_monitor_reset_window(monitor);
    memset(monitor->waterfall, 0, monitor->req.waterfall_bytes);
}

Js8MonitorStatus js8_monitor_process_block(Js8Monitor *monitor,
                                           const float samples[JS8_MONITOR_BLOCK_SIZE])
{
    uint32_t offset;
    uint32_t frame_pos = 0u;
    uint32_t time_sub;
    uint32_t physical_block;
    kiss_fft_cpx *freqdata;

    if (!monitor || !monitor->initialized)
        return JS8_MONITOR_ERR_NOT_INITIALIZED;
    if (!samples)
        return JS8_MONITOR_ERR_INVALID;
    for (unsigned i = 0; i < JS8_MONITOR_BLOCK_SIZE; ++i)
        if (!isfinite(samples[i]) || fabsf(samples[i]) > 1.0f)
            return JS8_MONITOR_ERR_INVALID;
    if (monitor->req.block_size != JS8_MONITOR_BLOCK_SIZE ||
        monitor->req.max_blocks == 0u)
        return JS8_MONITOR_ERR_INVALID;

    freqdata = (kiss_fft_cpx *)monitor->freq_scratch;
    if (monitor->num_blocks >= monitor->req.max_blocks)
        return JS8_MONITOR_WATERFALL_FULL;

    physical_block = monitor->num_blocks;
    offset = physical_block * monitor->req.block_stride;

    for (time_sub = 0u; time_sub < monitor->config.time_osr; ++time_sub) {
        uint32_t pos;
        uint32_t freq_sub;

        memmove(monitor->history,
                monitor->history + monitor->req.subblock_size,
                (monitor->req.nfft - monitor->req.subblock_size) * sizeof(float));
        for (pos = monitor->req.nfft - monitor->req.subblock_size;
             pos < monitor->req.nfft; ++pos) {
            monitor->history[pos] = samples[frame_pos++];
        }

        for (pos = 0u; pos < monitor->req.nfft; ++pos)
            monitor->time_scratch[pos] = monitor->window[pos] * monitor->history[pos];

        kiss_fftr((kiss_fftr_cfg)monitor->fft_cfg, monitor->time_scratch, freqdata);

        for (freq_sub = 0u; freq_sub < monitor->config.freq_osr; ++freq_sub) {
            uint32_t bin;
            for (bin = monitor->req.min_bin; bin < monitor->req.max_bin; ++bin) {
                uint32_t src_bin = bin * monitor->config.freq_osr + freq_sub;
                float mag2 = freqdata[src_bin].i * freqdata[src_bin].i +
                             freqdata[src_bin].r * freqdata[src_bin].r;
                float db = 10.0f * log10f(1E-12f + mag2);
                int scaled = (int)(2 * db + 240);
                monitor->waterfall[offset++] =
                    (uint8_t)((scaled < 0) ? 0 : ((scaled > 255) ? 255 : scaled));
                if (db > monitor->max_mag_db)
                    monitor->max_mag_db = db;
            }
        }
    }

    ++monitor->num_blocks;
    return JS8_MONITOR_OK;
}

Js8MonitorStatus js8_monitor_get_waterfall(const Js8Monitor *monitor,
                                           Js8WaterfallView *out_view)
{
    if (!monitor || !monitor->initialized)
        return JS8_MONITOR_ERR_NOT_INITIALIZED;
    if (!out_view)
        return JS8_MONITOR_ERR_INVALID;
    out_view->mag = monitor->waterfall;
    out_view->max_blocks = monitor->req.max_blocks;
    out_view->num_blocks = monitor->num_blocks;
    out_view->num_bins = monitor->req.num_bins;
    out_view->time_osr = monitor->config.time_osr;
    out_view->freq_osr = monitor->config.freq_osr;
    out_view->block_stride = monitor->req.block_stride;
    out_view->first_block = 0;
    return JS8_MONITOR_OK;
}

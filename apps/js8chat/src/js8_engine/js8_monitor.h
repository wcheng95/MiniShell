#ifndef JS8_MONITOR_H
#define JS8_MONITOR_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define JS8_MONITOR_SAMPLE_RATE_HZ 6000u
#define JS8_MONITOR_BLOCK_SIZE 960u
#define JS8_MONITOR_BASELINE_TIME_OSR 2u
#define JS8_MONITOR_BASELINE_FREQ_OSR 2u
#define JS8_MONITOR_BASELINE_F_MIN_HZ 200.0f
#define JS8_MONITOR_BASELINE_F_MAX_HZ 2900.0f

/* Linear waterfall: 93 JS8 Normal blocks = 14.88 s. With
 * time_osr=2 this is 186 stored 80-ms rows. */
#define JS8_MONITOR_LINEAR_BLOCKS 93u

typedef enum {
    JS8_MONITOR_OK = 0,
    JS8_MONITOR_ERR_INVALID = -1,
    JS8_MONITOR_ERR_WORKSPACE = -2,
    JS8_MONITOR_ERR_NOT_INITIALIZED = -3,
    /* Linear slot-local waterfall has reached its 93-block capacity. */
    JS8_MONITOR_WATERFALL_FULL = 1
} Js8MonitorStatus;

/* 6 kHz only. OSRs fit uint8 lanes; time_osr must divide 960 and freq_osr
 * must have only factors 2/3/5 (private no-heap FFT). Frequencies are finite,
 * 0 <= f_min < f_max < 3000. Query rejects unrepresentable workspace sizes.
 */
typedef struct {
    uint32_t sample_rate_hz;
    float f_min_hz;
    float f_max_hz;
    uint32_t time_osr;
    uint32_t freq_osr;
} Js8MonitorConfig;

typedef struct {
    size_t total_bytes;
    size_t alignment;
    size_t fft_plan_bytes;
    size_t waterfall_bytes;
    size_t window_bytes;
    size_t history_bytes;
    size_t time_scratch_bytes;
    size_t freq_scratch_bytes;
    uint32_t block_size;
    uint32_t subblock_size;
    uint32_t nfft;
    uint32_t min_bin;
    uint32_t max_bin;
    uint32_t num_bins;
    uint32_t max_blocks;
    uint32_t block_stride;
} Js8MonitorRequirements;

/* Linear view: mag points at the first stored block; first_block labels it.
 * The caller keeps num_blocks * block_stride bytes alive and unchanged during
 * search/decode. Views from the monitor start at logical block zero.
 */
typedef struct {
    const uint8_t *mag;
    uint32_t max_blocks;
    uint32_t num_blocks;
    uint32_t num_bins;
    uint32_t time_osr;
    uint32_t freq_osr;
    uint32_t block_stride;
    int32_t first_block;
} Js8WaterfallView;

typedef struct {
    int initialized;
    Js8MonitorConfig config;
    Js8MonitorRequirements req;
    float fft_norm;
    float max_mag_db;

    void *workspace;
    size_t workspace_bytes;
    void *fft_cfg;
    uint8_t *waterfall;
    float *window;
    float *history;
    float *time_scratch;
    void *freq_scratch;

    uint32_t num_blocks;
} Js8Monitor;

Js8MonitorConfig js8_monitor_baseline_config(void);
Js8MonitorStatus js8_monitor_query_requirements(const Js8MonitorConfig *config,
                                                Js8MonitorRequirements *out_req);
Js8MonitorStatus js8_monitor_init(Js8Monitor *monitor,
                                  const Js8MonitorConfig *config,
                                  void *workspace,
                                  size_t workspace_bytes);
void js8_monitor_destroy(Js8Monitor *monitor);

/* Rewind writer and clear FFT history/diagnostics; retain waterfall bytes.
 * Previously acquired views become stale after reset or further processing.
 */
void js8_monitor_reset_window(Js8Monitor *monitor);

/* A real stream discontinuity also clears stored waterfall bytes. */
void js8_monitor_reset_stream(Js8Monitor *monitor);

/* Samples must be finite and in [-1, 1]; invalid blocks leave state unchanged. */
Js8MonitorStatus js8_monitor_process_block(Js8Monitor *monitor,
                                           const float samples[JS8_MONITOR_BLOCK_SIZE]);

/* Generic linear view from buffer head. */
Js8MonitorStatus js8_monitor_get_waterfall(const Js8Monitor *monitor,
                                           Js8WaterfallView *out_view);

#ifdef __cplusplus
}
#endif

#endif

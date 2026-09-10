#ifndef FT8_MONITOR_H
#define FT8_MONITOR_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FT8_MONITOR_SAMPLE_RATE_HZ 6000u
#define FT8_MONITOR_BLOCK_SIZE 960u
#define FT8_MONITOR_BASELINE_TIME_OSR 2u
#define FT8_MONITOR_BASELINE_FREQ_OSR 2u
#define FT8_MONITOR_BASELINE_F_MIN_HZ 200.0f
#define FT8_MONITOR_BASELINE_F_MAX_HZ 2900.0f

typedef enum {
    FT8_MONITOR_OK = 0,
    FT8_MONITOR_ERR_INVALID = -1,
    FT8_MONITOR_ERR_WORKSPACE = -2,
    FT8_MONITOR_ERR_NOT_INITIALIZED = -3,
    FT8_MONITOR_WATERFALL_FULL = 1
} Ft8MonitorStatus;

typedef struct {
    uint32_t sample_rate_hz;
    float f_min_hz;
    float f_max_hz;
    uint32_t time_osr;
    uint32_t freq_osr;
} Ft8MonitorConfig;

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
} Ft8MonitorRequirements;

typedef struct {
    const uint8_t *mag;
    uint32_t max_blocks;
    uint32_t num_blocks;
    uint32_t num_bins;
    uint32_t time_osr;
    uint32_t freq_osr;
    uint32_t block_stride;
} Ft8WaterfallView;

typedef struct {
    int initialized;
    Ft8MonitorConfig config;
    Ft8MonitorRequirements req;
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
} Ft8Monitor;

Ft8MonitorConfig ft8_monitor_baseline_config(void);
Ft8MonitorStatus ft8_monitor_query_requirements(const Ft8MonitorConfig *config,
                                                Ft8MonitorRequirements *out_req);
Ft8MonitorStatus ft8_monitor_init(Ft8Monitor *monitor,
                                  const Ft8MonitorConfig *config,
                                  void *workspace,
                                  size_t workspace_bytes);
void ft8_monitor_destroy(Ft8Monitor *monitor);
void ft8_monitor_begin_window(Ft8Monitor *monitor);
void ft8_monitor_reset_stream(Ft8Monitor *monitor);
Ft8MonitorStatus ft8_monitor_process_block(Ft8Monitor *monitor,
                                           const float samples[FT8_MONITOR_BLOCK_SIZE]);
Ft8MonitorStatus ft8_monitor_get_waterfall(const Ft8Monitor *monitor,
                                           Ft8WaterfallView *out_view);

#ifdef __cplusplus
}
#endif

#endif

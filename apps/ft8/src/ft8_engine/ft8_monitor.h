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

/* V3 slot-local linear waterfall: 93 FT8 blocks = 14.88 s.  With
 * time_osr=2 this is 186 stored 80-ms rows. */
#define FT8_MONITOR_LINEAR_BLOCKS 93u

typedef enum {
    FT8_MONITOR_OK = 0,
    FT8_MONITOR_ERR_INVALID = -1,
    FT8_MONITOR_ERR_WORKSPACE = -2,
    FT8_MONITOR_ERR_NOT_INITIALIZED = -3,
    /* Linear slot-local waterfall has reached its 93-block capacity. */
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

/*
 * Linear waterfall view.  mag points at logical block 0. first_block may be
 * negative when mag is deliberately biased into the allocation (the V3 slot
 * view uses -10..+82 around the UTC origin). anchor_index is retained only for
 * source compatibility and is always zero on the linear path.
 */
typedef struct {
    const uint8_t *mag;
    uint32_t max_blocks;
    uint32_t num_blocks;
    uint32_t num_bins;
    uint32_t time_osr;
    uint32_t freq_osr;
    uint32_t block_stride;
    uint32_t anchor_index;
    int32_t first_block;
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

    /* Slot-local linear writer state. next_block_seq equals num_blocks and is
     * reset at UTC-1.6 s together with FFT history. */
    uint64_t next_block_seq;
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

/* UTC-1.6 s capture re-anchor: rewind the linear writer and FFT history.
 * Existing waterfall bytes are intentionally left intact so an older decode
 * can continue until the new writer reaches its UTC-origin region. */
void ft8_monitor_reset_window(Ft8Monitor *monitor);

/* Compatibility no-op at the actual UTC boundary. */
void ft8_monitor_begin_window(Ft8Monitor *monitor);

/* A real stream discontinuity also clears stored waterfall bytes. */
void ft8_monitor_reset_stream(Ft8Monitor *monitor);

Ft8MonitorStatus ft8_monitor_process_block(Ft8Monitor *monitor,
                                           const float samples[FT8_MONITOR_BLOCK_SIZE]);

/* Generic linear view from buffer head. */
Ft8MonitorStatus ft8_monitor_get_waterfall(const Ft8Monitor *monitor,
                                           Ft8WaterfallView *out_view);

/* Linear slot-relative view. anchor_seq is the block index from buffer head
 * corresponding to logical block zero; V3 uses anchor_seq=10. */
Ft8MonitorStatus ft8_monitor_get_waterfall_at(const Ft8Monitor *monitor,
                                              uint64_t anchor_seq,
                                              Ft8WaterfallView *out_view);

uint64_t ft8_monitor_next_block_sequence(const Ft8Monitor *monitor);

#ifdef __cplusplus
}
#endif

#endif

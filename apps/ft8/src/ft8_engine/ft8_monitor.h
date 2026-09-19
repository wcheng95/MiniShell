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

/* I001 continuous RX: keep at least one complete 15-second timeline plus
 * margin, while giving the ADV freq_osr=1 profile a 96 KiB waterfall ring. */
#define FT8_MONITOR_RING_MIN_BYTES (96u * 1024u)
#define FT8_MONITOR_RING_MIN_BLOCKS 94u

typedef enum {
    FT8_MONITOR_OK = 0,
    FT8_MONITOR_ERR_INVALID = -1,
    FT8_MONITOR_ERR_WORKSPACE = -2,
    FT8_MONITOR_ERR_NOT_INITIALIZED = -3,
    /* Retained for source compatibility; a circular monitor never becomes full. */
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
 * A logical view into the circular waterfall.
 *
 * anchor_index is the physical ring block corresponding to logical block 0.
 * first_block is the oldest retained logical block relative to that anchor;
 * num_blocks consecutive logical blocks are retained. Logical blocks may be
 * negative, which is how candidate search sees pre-slot continuity.
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

    /* next_block_seq is the sequence number assigned to the next completed
     * 960-sample waterfall block. num_blocks is the retained ring occupancy. */
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

/* Kept as a compatibility no-op: UTC slot boundaries never reset the ring. */
void ft8_monitor_begin_window(Ft8Monitor *monitor);

/* A real stream discontinuity clears FFT history and circular timeline state. */
void ft8_monitor_reset_stream(Ft8Monitor *monitor);

Ft8MonitorStatus ft8_monitor_process_block(Ft8Monitor *monitor,
                                           const float samples[FT8_MONITOR_BLOCK_SIZE]);

/* Generic retained view anchored at the oldest retained block. */
Ft8MonitorStatus ft8_monitor_get_waterfall(const Ft8Monitor *monitor,
                                           Ft8WaterfallView *out_view);

/* Slot-relative retained view anchored at an absolute monitor block sequence. */
Ft8MonitorStatus ft8_monitor_get_waterfall_at(const Ft8Monitor *monitor,
                                              uint64_t anchor_seq,
                                              Ft8WaterfallView *out_view);

uint64_t ft8_monitor_next_block_sequence(const Ft8Monitor *monitor);

#ifdef __cplusplus
}
#endif

#endif

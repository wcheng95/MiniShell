#ifndef SSTV_CORE_H
#define SSTV_CORE_H

#include <stddef.h>
#include <stdint.h>

#define SSTV_SAMPLE_RATE 12000u
#define SSTV_ROBOT36_WIDTH 320u
#define SSTV_ROBOT36_HEIGHT 240u
#define SSTV_ROBOT36_VIS 8u

typedef struct {
    void *ctx;
    int (*begin)(void *ctx, unsigned width, unsigned height);
    int (*row)(void *ctx, unsigned y, const uint8_t *rgb, unsigned width);
    int (*end)(void *ctx, int complete);
} SstvImageSink;

typedef enum {
    SSTV_RESULT_RUNNING = 0,
    SSTV_RESULT_COMPLETE = 1,
    SSTV_RESULT_UNSUPPORTED = -1,
    SSTV_RESULT_BAD_VIS = -2,
    SSTV_RESULT_SINK_ERROR = -3,
    SSTV_RESULT_TRUNCATED = -4
} SstvResult;

typedef enum {
    SSTV_STATE_LEADER = 0,
    SSTV_STATE_BREAK,
    SSTV_STATE_SECOND_LEADER,
    SSTV_STATE_VIS_START,
    SSTV_STATE_VIS,
    SSTV_STATE_IMAGE,
    SSTV_STATE_DONE,
    SSTV_STATE_ERROR
} SstvState;

typedef struct {
    SstvImageSink sink;
    SstvState state;
    SstvResult result;

    int16_t hilbert_ring[19];
    unsigned hilbert_pos;
    unsigned hilbert_fill;
    float prev_i;
    float prev_q;
    int have_prev_iq;
    float frequency_hz;
    float previous_frequency_hz;
    uint64_t sample_index;

    unsigned tone_run;
    double tone_sum;
    float frequency_offset_hz;

    uint64_t vis_start_sample;
    double vis_sum[10];
    unsigned vis_count[10];
    uint8_t vis_code;

    unsigned line;
    double line_start;
    double line_period;
    double last_sync_start;
    int have_last_sync;
    unsigned sync_run;
    uint64_t sync_run_start;
    double sync_sum;
    unsigned sync_count;

    double y_sum[SSTV_ROBOT36_WIDTH];
    uint16_t y_count[SSTV_ROBOT36_WIDTH];
    double c_sum[SSTV_ROBOT36_WIDTH];
    uint16_t c_count[SSTV_ROBOT36_WIDTH];
    double separator_sum;
    unsigned separator_count;
    double line_sync_sum;
    unsigned line_sync_count;

    uint8_t y_even[SSTV_ROBOT36_WIDTH];
    uint8_t y_odd[SSTV_ROBOT36_WIDTH];
    uint8_t cr[SSTV_ROBOT36_WIDTH];
    uint8_t cb[SSTV_ROBOT36_WIDTH];
    uint8_t rgb_row[3u * SSTV_ROBOT36_WIDTH];
    int have_even;
} SstvCore;

void sstv_core_init(SstvCore *core, const SstvImageSink *sink);
SstvResult sstv_core_process(SstvCore *core, const int16_t *samples, size_t count);
SstvResult sstv_core_finish(SstvCore *core);
const char *sstv_result_string(SstvResult result);

#endif

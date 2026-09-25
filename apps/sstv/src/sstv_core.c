#include "sstv_core.h"

#include <math.h>
#include <string.h>

#define PI_F 3.14159265358979323846f
#define HILBERT_N 19u
#define HILBERT_DELAY 9u
#define VIS_CELL_SAMPLES 360u
#define ROBOT36_LINE_SAMPLES 1800.0

static const float hilbert[HILBERT_N] = {
    -0.005658842421f, 0.0f, -0.017063188448f, 0.0f, -0.058584531989f,
    0.0f, -0.163399074908f, 0.0f, -0.618959052155f, 0.0f,
    0.618959052155f, 0.0f, 0.163399074908f, 0.0f, 0.058584531989f,
    0.0f, 0.017063188448f, 0.0f, 0.005658842421f
};

static int near_hz(float f, float target, float tolerance)
{
    return fabsf(f - target) <= tolerance;
}

static uint8_t clamp_byte(double v)
{
    if (v <= 0.0) return 0;
    if (v >= 255.0) return 255;
    return (uint8_t)lrint(v);
}

static uint8_t freq_to_byte(double f)
{
    return clamp_byte((f - 1500.0) * (255.0 / 800.0));
}

static void reset_tone(SstvCore *c)
{
    c->tone_run = 0;
    c->tone_sum = 0.0;
}

static void reset_line_accumulators(SstvCore *c)
{
    memset(c->y_sum, 0, sizeof(c->y_sum));
    memset(c->y_count, 0, sizeof(c->y_count));
    memset(c->c_sum, 0, sizeof(c->c_sum));
    memset(c->c_count, 0, sizeof(c->c_count));
    c->separator_sum = 0.0;
    c->separator_count = 0;
    c->line_sync_sum = 0.0;
    c->line_sync_count = 0;
}

static void fail(SstvCore *c, SstvResult result)
{
    c->result = result;
    c->state = SSTV_STATE_ERROR;
    if (c->sink.end) (void)c->sink.end(c->sink.ctx, 0);
}

static int emit_rgb_row(SstvCore *c, unsigned row, const uint8_t *y)
{
    for (unsigned x = 0; x < SSTV_ROBOT36_WIDTH; ++x) {
        const double yy = y[x];
        const double cr = (double)c->cr[x] - 128.0;
        const double cb = (double)c->cb[x] - 128.0;
        c->rgb_row[3u*x+0u] = clamp_byte(yy + 1.402 * cr);
        c->rgb_row[3u*x+1u] = clamp_byte(yy - 0.344136 * cb - 0.714136 * cr);
        c->rgb_row[3u*x+2u] = clamp_byte(yy + 1.772 * cb);
    }
    return !c->sink.row || c->sink.row(c->sink.ctx, row, c->rgb_row, SSTV_ROBOT36_WIDTH) == 0;
}

static void finalize_line(SstvCore *c)
{
    uint8_t *y_dst = (c->line & 1u) ? c->y_odd : c->y_even;
    uint8_t *c_dst = (c->line & 1u) ? c->cb : c->cr;

    for (unsigned x = 0; x < SSTV_ROBOT36_WIDTH; ++x) {
        const double yv = c->y_count[x] ? c->y_sum[x] / c->y_count[x] : 1500.0;
        const double cv = c->c_count[x] ? c->c_sum[x] / c->c_count[x] : 1901.6;
        y_dst[x] = freq_to_byte(yv - c->frequency_offset_hz);
        c_dst[x] = freq_to_byte(cv - c->frequency_offset_hz);
    }

    if (c->line_sync_count) {
        const double observed = c->line_sync_sum / c->line_sync_count;
        c->frequency_offset_hz =
            (float)(0.98 * c->frequency_offset_hz + 0.02 * (observed - 1200.0));
    }

    if ((c->line & 1u) == 0u) {
        c->have_even = 1;
    } else if (c->have_even) {
        if (!emit_rgb_row(c, c->line - 1u, c->y_even) ||
            !emit_rgb_row(c, c->line, c->y_odd)) {
            fail(c, SSTV_RESULT_SINK_ERROR);
            return;
        }
        c->have_even = 0;
    }

    ++c->line;
    if (c->line >= SSTV_ROBOT36_HEIGHT) {
        if (c->sink.end && c->sink.end(c->sink.ctx, 1) != 0) {
            fail(c, SSTV_RESULT_SINK_ERROR);
            return;
        }
        c->result = SSTV_RESULT_COMPLETE;
        c->state = SSTV_STATE_DONE;
        return;
    }

    c->line_start += c->line_period;
    reset_line_accumulators(c);
}

static void image_sample(SstvCore *c, float corrected_hz)
{
    if (c->state != SSTV_STATE_IMAGE) return;

    double rel = (double)c->sample_index - c->line_start;
    while (rel >= c->line_period && c->state == SSTV_STATE_IMAGE) {
        finalize_line(c);
        if (c->state != SSTV_STATE_IMAGE) return;
        rel = (double)c->sample_index - c->line_start;
    }
    if (rel < 0.0) return;

    const double scale = c->line_period / ROBOT36_LINE_SAMPLES;
    const double sync_end = 108.0 * scale;
    const double y_start = 144.0 * scale;
    const double y_end = 1200.0 * scale;
    const double sep_end = 1254.0 * scale;
    const double chroma_start = 1272.0 * scale;
    const double chroma_end = 1800.0 * scale;

    if (rel < sync_end) {
        c->line_sync_sum += c->frequency_hz;
        ++c->line_sync_count;
    } else if (rel >= y_start && rel < y_end) {
        const double pixel_width = (y_end - y_start) / SSTV_ROBOT36_WIDTH;
        unsigned x = (unsigned)((rel - y_start) / pixel_width);
        if (x >= SSTV_ROBOT36_WIDTH) x = SSTV_ROBOT36_WIDTH - 1u;
        c->y_sum[x] += c->frequency_hz;
        ++c->y_count[x];
    } else if (rel >= y_end && rel < sep_end) {
        c->separator_sum += corrected_hz;
        ++c->separator_count;
    } else if (rel >= chroma_start && rel < chroma_end) {
        const double pixel_width = (chroma_end - chroma_start) / SSTV_ROBOT36_WIDTH;
        unsigned x = (unsigned)((rel - chroma_start) / pixel_width);
        if (x >= SSTV_ROBOT36_WIDTH) x = SSTV_ROBOT36_WIDTH - 1u;
        c->c_sum[x] += c->frequency_hz;
        ++c->c_count[x];
    }

    /* A 1200-Hz run near a predicted boundary provides slow line-clock tracking.
       Confirmation occurs inside the sync pulse, before image samples begin. */
    const double window = 48.0;
    if (rel < 140.0 || rel > c->line_period - window) {
        if (near_hz(corrected_hz, 1200.0f, 95.0f)) {
            if (!c->sync_run) c->sync_run_start = c->sample_index;
            ++c->sync_run;
            c->sync_sum += c->frequency_hz;
            ++c->sync_count;
        } else {
            if (c->sync_run >= 24u && c->line > 0u) {
                const double candidate = (double)c->sync_run_start;
                const double error = candidate - c->line_start;
                if (fabs(error) <= window) {
                    if (c->have_last_sync) {
                        double observed = candidate - c->last_sync_start;
                        if (observed < 1782.0) observed = 1782.0;
                        if (observed > 1818.0) observed = 1818.0;
                        c->line_period = 0.95 * c->line_period + 0.05 * observed;
                    }
                    c->last_sync_start = candidate;
                    c->have_last_sync = 1;
                    c->line_start += 0.25 * error;
                }
            }
            c->sync_run = 0;
            c->sync_sum = 0.0;
            c->sync_count = 0;
        }
    }
}

static void process_vis(SstvCore *c, float corrected_hz)
{
    const uint64_t elapsed = c->sample_index - c->vis_start_sample;
    unsigned cell = (unsigned)(elapsed / VIS_CELL_SAMPLES);
    if (cell >= 10u) {
        double avg[10];
        for (unsigned i = 0; i < 10u; ++i) {
            if (!c->vis_count[i]) {
                fail(c, SSTV_RESULT_BAD_VIS);
                return;
            }
            avg[i] = c->vis_sum[i] / c->vis_count[i];
        }
        if (fabs(avg[0]-1200.0) > 100.0 || fabs(avg[9]-1200.0) > 100.0) {
            fail(c, SSTV_RESULT_BAD_VIS);
            return;
        }
        unsigned ones = 0;
        uint8_t code = 0;
        for (unsigned i = 0; i < 7u; ++i) {
            const double f = avg[i+1u];
            const unsigned bit = fabs(f-1100.0) < fabs(f-1300.0);
            if (bit) {
                code |= (uint8_t)(1u << i);
                ++ones;
            }
        }
        const unsigned parity = fabs(avg[8]-1100.0) < fabs(avg[8]-1300.0);
        if (((ones + parity) & 1u) != 0u) {
            fail(c, SSTV_RESULT_BAD_VIS);
            return;
        }
        c->vis_code = code;
        if (code != SSTV_ROBOT36_VIS) {
            fail(c, SSTV_RESULT_UNSUPPORTED);
            return;
        }
        if (c->sink.begin &&
            c->sink.begin(c->sink.ctx, SSTV_ROBOT36_WIDTH, SSTV_ROBOT36_HEIGHT) != 0) {
            fail(c, SSTV_RESULT_SINK_ERROR);
            return;
        }
        c->state = SSTV_STATE_IMAGE;
        c->line = 0;
        c->line_period = ROBOT36_LINE_SAMPLES;
        c->line_start = (double)c->vis_start_sample + 10.0 * VIS_CELL_SAMPLES;
        c->last_sync_start = c->line_start;
        c->have_last_sync = 1;
        reset_line_accumulators(c);
        image_sample(c, corrected_hz);
        return;
    }

    const unsigned phase = (unsigned)(elapsed % VIS_CELL_SAMPLES);
    if (phase >= 72u && phase < 288u) {
        c->vis_sum[cell] += corrected_hz;
        ++c->vis_count[cell];
    }
}

static void protocol_sample(SstvCore *c, float f)
{
    float corrected = f - c->frequency_offset_hz;

    switch (c->state) {
    case SSTV_STATE_LEADER:
        if (f >= 1700.0f && f <= 2100.0f) {
            ++c->tone_run;
            c->tone_sum += f;
            if (c->tone_run >= 1800u) {
                c->frequency_offset_hz = (float)(c->tone_sum / c->tone_run - 1900.0);
                c->state = SSTV_STATE_BREAK;
                reset_tone(c);
            }
        } else {
            reset_tone(c);
        }
        break;

    case SSTV_STATE_BREAK:
        corrected = f - c->frequency_offset_hz;
        if (near_hz(corrected,1200.0f,100.0f)) {
            if (++c->tone_run >= 60u) {
                c->state=SSTV_STATE_SECOND_LEADER;
                reset_tone(c);
            }
        } else {
            reset_tone(c);
        }
        break;

    case SSTV_STATE_SECOND_LEADER:
        corrected = f - c->frequency_offset_hz;
        if (near_hz(corrected,1900.0f,120.0f)) {
            ++c->tone_run;
            c->tone_sum += f;
            if (c->tone_run >= 1800u) {
                c->frequency_offset_hz =
                    (float)(0.5*c->frequency_offset_hz +
                            0.5*(c->tone_sum/c->tone_run-1900.0));
                c->state=SSTV_STATE_VIS_START;
                reset_tone(c);
            }
        } else {
            reset_tone(c);
        }
        break;

    case SSTV_STATE_VIS_START:
        corrected = f - c->frequency_offset_hz;
        if (near_hz(corrected,1200.0f,100.0f)) {
            if (!c->tone_run) c->vis_start_sample = c->sample_index;
            ++c->tone_run;
            if (c->tone_run >= 24u) {
                memset(c->vis_sum,0,sizeof(c->vis_sum));
                memset(c->vis_count,0,sizeof(c->vis_count));
                c->state=SSTV_STATE_VIS;
                c->vis_start_sample = c->sample_index - c->tone_run + 1u;
                reset_tone(c);
                process_vis(c, corrected);
            }
        } else {
            reset_tone(c);
        }
        break;

    case SSTV_STATE_VIS:
        process_vis(c, corrected);
        break;

    case SSTV_STATE_IMAGE:
        image_sample(c, corrected);
        break;

    case SSTV_STATE_DONE:
    case SSTV_STATE_ERROR:
        break;
    }
}

static int demod_sample(SstvCore *c, int16_t x, float *out_frequency)
{
    const unsigned pos = c->hilbert_pos;
    c->hilbert_ring[pos] = x;
    if (c->hilbert_fill < HILBERT_N) ++c->hilbert_fill;

    float q = 0.0f;
    for (unsigned k=0; k<HILBERT_N; ++k) {
        const unsigned idx = (pos + HILBERT_N - k) % HILBERT_N;
        q += hilbert[k] * (float)c->hilbert_ring[idx];
    }
    const unsigned iidx = (pos + HILBERT_N - HILBERT_DELAY) % HILBERT_N;
    const float i = (float)c->hilbert_ring[iidx];
    c->hilbert_pos = (pos + 1u) % HILBERT_N;

    if (c->hilbert_fill < HILBERT_N) return 0;
    if (!c->have_prev_iq) {
        c->prev_i=i;
        c->prev_q=q;
        c->have_prev_iq=1;
        return 0;
    }
    const float cross = q*c->prev_i - i*c->prev_q;
    const float dot = i*c->prev_i + q*c->prev_q;
    c->prev_i=i;
    c->prev_q=q;
    if (fabsf(cross)+fabsf(dot) < 1000.0f) return 0;
    float f = atan2f(cross,dot) * (float)SSTV_SAMPLE_RATE / (2.0f*PI_F);
    if (f < 500.0f || f > 2700.0f) return 0;
    *out_frequency=f;
    return 1;
}

void sstv_core_init(SstvCore *c, const SstvImageSink *sink)
{
    memset(c,0,sizeof(*c));
    if (sink) c->sink=*sink;
    c->state=SSTV_STATE_LEADER;
    c->result=SSTV_RESULT_RUNNING;
    c->line_period=ROBOT36_LINE_SAMPLES;
}

SstvResult sstv_core_process(SstvCore *c, const int16_t *samples, size_t count)
{
    if (!c || (!samples && count)) return SSTV_RESULT_BAD_VIS;
    for (size_t n=0; n<count &&
         c->state!=SSTV_STATE_DONE && c->state!=SSTV_STATE_ERROR; ++n) {
        float f;
        if (demod_sample(c,samples[n],&f)) {
            c->previous_frequency_hz=c->frequency_hz;
            c->frequency_hz=f;
            protocol_sample(c,f);
        }
        ++c->sample_index;
    }
    return c->result;
}

SstvResult sstv_core_finish(SstvCore *c)
{
    if (!c) return SSTV_RESULT_TRUNCATED;
    if (c->state==SSTV_STATE_DONE || c->state==SSTV_STATE_ERROR) return c->result;
    if (c->sink.end) (void)c->sink.end(c->sink.ctx,0);
    c->result=SSTV_RESULT_TRUNCATED;
    c->state=SSTV_STATE_ERROR;
    return c->result;
}

const char *sstv_result_string(SstvResult r)
{
    switch(r) {
    case SSTV_RESULT_RUNNING: return "running";
    case SSTV_RESULT_COMPLETE: return "complete";
    case SSTV_RESULT_UNSUPPORTED: return "unsupported VIS mode";
    case SSTV_RESULT_BAD_VIS: return "invalid VIS header";
    case SSTV_RESULT_SINK_ERROR: return "image output error";
    case SSTV_RESULT_TRUNCATED: return "incomplete SSTV image";
    }
    return "unknown SSTV error";
}

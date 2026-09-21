/* Mini-CW 3bfbf169b7c2d49a1be3e9a4c80f945edb32033e:
 * original DDS, envelope, segment cursor and committed-sample accounting.
 * Only hardware synchronization, text expansion and task lifecycle are split. */
#include "tone_stream.h"
#include <math.h>
#include <string.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define AUDIO_CW_AMPLITUDE 12000
#define AUDIO_CW_ENVELOPE_MS 5
#define AUDIO_SEG_RING_CAP 64
#define DDS_QTABLE_BITS 8u
#define DDS_FRAC_BITS 10u
#define DDS_QTABLE_SIZE (1u << DDS_QTABLE_BITS)
#define DDS_VISIBLE_BITS (2u + DDS_QTABLE_BITS + DDS_FRAC_BITS)
#define DDS_PHASE_BITS 64u
typedef enum {
    SEG_SILENCE = 0,
    SEG_TONE,
    SEG_TONE_HOLD,
} seg_kind_t;

typedef struct {
    seg_kind_t kind;
    uint32_t duration_samples;
    bool attack;
    bool release;
} audio_seg_t;

static tone_stream_port_t s_port;
static uint16_t s_pitch_hz;
static const struct { uint32_t sample_rate_hz; } s_output_config = {48000};
static int16_t s_sin_quarter[DDS_QTABLE_SIZE + 1];
static uint64_t s_phase;
/* Segment ring (guarded by s_seg_mutex). */
static audio_seg_t s_seg_ring[AUDIO_SEG_RING_CAP];
static uint16_t s_seg_head;
static uint16_t s_seg_tail;
static uint16_t s_seg_count;

/* Stream control flags (guarded by s_seg_mutex unless noted). */
static volatile bool s_flush_requested;   /* ramp current tone down, then drop */
static volatile bool s_hold_active;       /* a straight-key hold is in flight */

/* Render-loop-private cursor (touched only by the audio task). */
static audio_seg_t s_cur;
static bool s_cur_valid;
static uint32_t s_cur_pos;
static uint32_t s_cur_attack_n;
static uint32_t s_cur_release_n;
static bool s_rel_mode;        /* current segment is a flush/release tail */
static uint16_t s_rel_g0;      /* gain the release tail starts from */
static uint32_t s_cur_gen;     /* busy generation the cursor's tone belongs to */

/* is_busy() source of truth: keyed samples enqueued but not yet emitted. */
static volatile uint32_t s_keyed_outstanding;
static volatile uint32_t s_busy_gen;
static uint64_t audio_cw_phase_inc(void)
{
    /* inc = f * 2^64 / fs, matching dds_q15 inc_from_hz(). */
    long double num = (long double)s_pitch_hz * (long double)(1ULL << 63) * 2.0L;
    long double den = (long double)s_output_config.sample_rate_hz;

    if (den <= 0.0L) {
        return 0;
    }

    return (uint64_t)llroundl(num / den);
}

static void audio_cw_init_sine_lut(void)
{
    const double step = (M_PI / 2.0) / (double)DDS_QTABLE_SIZE;

    for (unsigned n = 0; n <= DDS_QTABLE_SIZE; ++n) {
        double s = sin(step * (double)n);
        int32_t q = (int32_t)lround(s * 32767.0);
        if (q > 32767) {
            q = 32767;
        }
        if (q < -32768) {
            q = -32768;
        }
        s_sin_quarter[n] = (int16_t)q;
    }
}

/* Sample sin(phase) from the quarter-wave LUT with linear interpolation. */
static inline int16_t audio_cw_sin_q15(uint64_t phase)
{
    uint32_t v = (uint32_t)(phase >> (DDS_PHASE_BITS - DDS_VISIBLE_BITS));
    uint32_t quad = v >> (DDS_QTABLE_BITS + DDS_FRAC_BITS);                  /* 0..3 */
    uint32_t xf = v & ((1u << (DDS_QTABLE_BITS + DDS_FRAC_BITS)) - 1u);
    uint32_t idx = xf >> DDS_FRAC_BITS;
    uint32_t frac = xf & ((1u << DDS_FRAC_BITS) - 1u);

    /* Mirror in odd quadrants (Q1, Q3): position = QTABLE_SIZE - position. */
    if (quad & 1u) {
        idx = DDS_QTABLE_SIZE - ((xf + ((1u << DDS_FRAC_BITS) - 1u)) >> DDS_FRAC_BITS);
        frac = (0u - frac) & ((1u << DDS_FRAC_BITS) - 1u);
    }

    int32_t y;
    if (idx >= DDS_QTABLE_SIZE) {
        y = s_sin_quarter[DDS_QTABLE_SIZE];
    } else {
        int32_t y0 = s_sin_quarter[idx];
        int32_t y1 = s_sin_quarter[idx + 1];
        int32_t dif = y1 - y0;
        int32_t acc = dif * (int32_t)frac;
        y = y0 + ((acc + (1 << (DDS_FRAC_BITS - 1))) >> DDS_FRAC_BITS);
    }

    if (quad >= 2u) {
        y = -y;
    }

    return (int16_t)y;
}

/*
 * Raised-cosine rising-edge gain (Q15) at sample i of an n-sample edge:
 * (1 - cos(pi*i/n)) / 2 == sin^2(pi*i/(2n)). The quarter-wave sine LUT spans
 * exactly [0, pi/2], so squaring its lookup yields the envelope without a
 * second table. A falling edge is the same curve indexed as (n - 1 - i), so
 * the last sample lands on exact silence, matching copy_trainer.py.
 */
static uint16_t audio_cw_rcos_rise_q15(uint32_t i, uint32_t n)
{
    if (n == 0 || i >= n) {
        return 32767;
    }

    uint32_t idx = (uint32_t)(((uint64_t)i * DDS_QTABLE_SIZE) / n);
    int32_t s = s_sin_quarter[idx];
    return (uint16_t)(((int32_t)s * s) >> 15);
}

/* Quarter-wave sine scaled by the Q15 key envelope and the tone amplitude. */
static int16_t audio_cw_render_sample(uint64_t phase, uint16_t env_q15)
{
    int32_t s = audio_cw_sin_q15(phase);
    s = (s * (int32_t)env_q15) >> 15;
    s = (s * AUDIO_CW_AMPLITUDE) >> 15;
    return (int16_t)s;
}

static void audio_cw_calculate_envelope_samples(uint32_t total_samples,
                                                uint32_t sample_rate,
                                                uint32_t *attack_samples,
                                                uint32_t *release_samples)
{
    /*
     * The click-reduction envelope is defined in milliseconds, then converted
     * to samples using the active output sample rate. Very short tones cannot
     * fit a full attack plus release, so the envelope is scaled to the tone
     * length instead of exceeding it.
     */
    uint32_t nominal_samples =
        (uint32_t)(((uint64_t)sample_rate * AUDIO_CW_ENVELOPE_MS) / 1000U);

    if (nominal_samples == 0 && total_samples > 0) {
        nominal_samples = 1;
    }

    *attack_samples = nominal_samples;
    *release_samples = nominal_samples;

    if (*attack_samples + *release_samples > total_samples) {
        *attack_samples = total_samples / 2U;
        *release_samples = total_samples - *attack_samples;
    }
}

static uint16_t audio_cw_envelope_gain_q15(uint32_t sample_index,
                                           uint32_t total_samples,
                                           uint32_t attack_samples,
                                           uint32_t release_samples)
{
    uint16_t gain = 32767;

    if (total_samples == 0) {
        return 0;
    }

    if (attack_samples > 0 && sample_index < attack_samples) {
        gain = audio_cw_rcos_rise_q15(sample_index, attack_samples);
    }

    if (release_samples > 0) {
        const uint32_t release_start = total_samples - release_samples;
        if (sample_index >= release_start) {
            uint32_t pos_in_release = sample_index - release_start;
            uint16_t release_gain =
                audio_cw_rcos_rise_q15(release_samples - 1U - pos_in_release, release_samples);
            if (release_gain < gain) {
                gain = release_gain;
            }
        }
    }

    return gain;
}

static uint32_t audio_cw_ms_to_samples(uint32_t ms)
{
    return (uint32_t)(((uint64_t)s_output_config.sample_rate_hz * ms) / 1000U);
}

static bool audio_cw_lock(uint32_t timeout) { return s_port.lock(timeout); }
static void audio_cw_unlock(void) { s_port.unlock(); }
static uint32_t audio_cw_busy_generation(void)
{
    return s_busy_gen;
}

static void audio_cw_busy_add(uint32_t samples)
{
    s_port.busy_enter();
    s_keyed_outstanding += samples;
    s_port.busy_exit();
}

/*
 * Subtract emitted keyed samples, but only if no preempt cleared the counter
 * since this tally's element was loaded into the cursor. A flush bumps the
 * generation under the same spinlock as the replacement element's busy_add, so
 * a tally accumulated for a since-preempted element fails the match and is
 * dropped instead of being charged against the new element's fresh count.
 */
static void audio_cw_busy_sub(uint32_t samples, uint32_t gen)
{
    s_port.busy_enter();
    if (s_busy_gen == gen) {
        s_keyed_outstanding = (s_keyed_outstanding > samples) ? (s_keyed_outstanding - samples) : 0U;
    }
    s_port.busy_exit();
}

static void audio_cw_busy_clear(void)
{
    s_port.busy_enter();
    s_keyed_outstanding = 0U;
    s_busy_gen++;
    s_port.busy_exit();
}

/* Ring helpers; caller must hold s_seg_mutex. */
static bool audio_cw_seg_push(const audio_seg_t *seg)
{
    if (s_seg_count >= AUDIO_SEG_RING_CAP) {
        return false;
    }

    s_seg_ring[s_seg_tail] = *seg;
    s_seg_tail = (uint16_t)((s_seg_tail + 1U) % AUDIO_SEG_RING_CAP);
    s_seg_count++;
    return true;
}

static bool audio_cw_seg_pop(audio_seg_t *out)
{
    if (s_seg_count == 0) {
        return false;
    }

    *out = s_seg_ring[s_seg_head];
    s_seg_head = (uint16_t)((s_seg_head + 1U) % AUDIO_SEG_RING_CAP);
    s_seg_count--;
    return true;
}

static void audio_cw_seg_clear(void)
{
    s_seg_head = 0;
    s_seg_tail = 0;
    s_seg_count = 0;
}

/* Drop everything queued and ask the loop to ramp the live tone down cleanly. */
static void audio_cw_preempt_locked(void)
{
    audio_cw_seg_clear();
    s_hold_active = false;
    audio_cw_busy_clear();
    s_flush_requested = true;
}

/* Push a finite keyed tone of N samples (counts toward is_busy). */
static void audio_cw_push_tone_locked(uint32_t samples)
{
    if (samples == 0) {
        return;
    }

    audio_seg_t seg = {
        .kind = SEG_TONE,
        .duration_samples = samples,
        .attack = true,
        .release = true,
    };

    if (audio_cw_seg_push(&seg)) {
        audio_cw_busy_add(samples);
    } else {
        /* Public enqueue checks capacity while holding the same mutex. */
    }
}

/* Gain the current cursor would emit at s_cur_pos right now. */
static uint16_t audio_cw_cursor_gain(void)
{
    if (s_rel_mode) {
        uint16_t fall = audio_cw_rcos_rise_q15(s_cur_release_n - 1U - s_cur_pos, s_cur_release_n);
        return (uint16_t)(((uint32_t)s_rel_g0 * fall) >> 15);
    }

    if (s_cur.kind == SEG_TONE_HOLD) {
        return (s_cur_pos < s_cur_attack_n)
                   ? audio_cw_rcos_rise_q15(s_cur_pos, s_cur_attack_n)
                   : 32767;
    }

    return audio_cw_envelope_gain_q15(s_cur_pos,
                                      s_cur.duration_samples,
                                      s_cur_attack_n,
                                      s_cur_release_n);
}

/* Convert whatever is sounding into a short raised-cosine release tail. */
static void audio_cw_begin_release_locked(void)
{
    if (!s_cur_valid || s_cur.kind == SEG_SILENCE) {
        s_cur_valid = false;
        s_rel_mode = false;
        return;
    }

    uint16_t g0 = audio_cw_cursor_gain();
    uint32_t rel = audio_cw_ms_to_samples(AUDIO_CW_ENVELOPE_MS);
    if (rel == 0) {
        rel = 1;
    }

    s_rel_g0 = g0;
    s_cur.kind = SEG_TONE;
    s_cur.duration_samples = rel;
    s_cur.attack = false;
    s_cur.release = true;
    s_cur_pos = 0;
    s_cur_attack_n = 0;
    s_cur_release_n = rel;
    s_rel_mode = true;
    s_cur_valid = true;
}

/* Pop the next segment into the cursor; returns false if the ring is empty. */
static bool audio_cw_refill_cursor(void)
{
    audio_seg_t seg;
    bool got = false;

    if (audio_cw_lock(UINT32_MAX)) {
        got = audio_cw_seg_pop(&seg);
        audio_cw_unlock();
    }

    if (!got) {
        return false;
    }

    s_cur = seg;
    s_cur_pos = 0;
    s_rel_mode = false;
    s_cur_gen = audio_cw_busy_generation();

    if (seg.kind == SEG_TONE) {
        uint32_t attack_n = 0;
        uint32_t release_n = 0;
        audio_cw_calculate_envelope_samples(seg.duration_samples,
                                            (uint32_t)s_output_config.sample_rate_hz,
                                            &attack_n,
                                            &release_n);
        s_cur_attack_n = seg.attack ? attack_n : 0;
        s_cur_release_n = seg.release ? release_n : 0;
    } else if (seg.kind == SEG_TONE_HOLD) {
        uint32_t env = audio_cw_ms_to_samples(AUDIO_CW_ENVELOPE_MS);
        s_cur_attack_n = env > 0 ? env : 1;
        s_cur_release_n = 0;
    }

    s_cur_valid = true;
    return true;
}


void tone_stream_render(int16_t buf[TONE_STREAM_FRAMES], tone_stream_commit_t *commit)
{
    const uint32_t chunk = TONE_STREAM_FRAMES;
    uint64_t phase_inc = 0;

    if (audio_cw_lock(UINT32_MAX)) {
        phase_inc = audio_cw_phase_inc();
        if (s_flush_requested) {
            s_flush_requested = false;
            audio_cw_begin_release_locked();
        }
        audio_cw_unlock();
    }

    uint32_t keyed_tally = 0;
    uint32_t keyed_gen = 0;
    uint32_t i = 0;
    while (i < chunk) {
        if (!s_cur_valid && !audio_cw_refill_cursor()) {
            /* Ring empty: the rest of this chunk is silence. */
            while (i < chunk) {
                buf[i++] = 0;
            }
            break;
        }

        if (s_cur.kind == SEG_SILENCE) {
            buf[i] = 0;
            s_cur_pos++;
            if (s_cur_pos >= s_cur.duration_samples) {
                s_cur_valid = false;
            }
        } else {
            uint16_t gain = audio_cw_cursor_gain();
            buf[i] = audio_cw_render_sample(s_phase, gain);
            s_phase += phase_inc;

            if (s_cur.kind == SEG_TONE && !s_rel_mode) {
                if (keyed_tally == 0) {
                    keyed_gen = s_cur_gen;
                }
                keyed_tally++;
            }

            if (s_cur.kind == SEG_TONE_HOLD) {
                if (s_cur_pos < s_cur_attack_n) {
                    s_cur_pos++; /* freeze position once the attack completes */
                }
            } else {
                s_cur_pos++;
                if (s_cur_pos >= s_cur.duration_samples) {
                    s_rel_mode = false;
                    s_cur_valid = false;
                }
            }
        }

        i++;
    }


    commit->samples = keyed_tally;
    commit->generation = keyed_gen;
}
void tone_stream_committed(const tone_stream_commit_t *commit)
{
    if (commit->samples) audio_cw_busy_sub(commit->samples, commit->generation);
}
void tone_stream_init(const tone_stream_port_t *port, uint16_t hz)
{
    s_port = *port;
    s_pitch_hz = hz;
    s_phase = 0;
    audio_cw_init_sine_lut();
    audio_cw_seg_clear();
    s_flush_requested = s_hold_active = s_cur_valid = s_rel_mode = false;
    memset(&s_cur, 0, sizeof(s_cur));
    s_cur_pos = s_cur_attack_n = s_cur_release_n = s_cur_gen = s_rel_g0 = 0;
    s_keyed_outstanding = s_busy_gen = 0;
}
mini_result_t tone_stream_pitch(uint16_t hz)
{
    if (hz < 300 || hz > 999) return MINI_ERR_INVALID;
    if (!audio_cw_lock(20)) return MINI_ERR_TIMEOUT;
    s_pitch_hz = hz;
    audio_cw_unlock();
    return MINI_OK;
}
mini_result_t tone_stream_enqueue(uint32_t ms)
{
    /* Bound total sample accounting for a completely full ring. */
    if (!ms || ms > 60000) return MINI_ERR_INVALID;
    if (!audio_cw_lock(20)) return MINI_ERR_TIMEOUT;
    mini_result_t result = MINI_OK;
    if (s_seg_count == AUDIO_SEG_RING_CAP) result = MINI_ERR_NO_SPACE;
    else audio_cw_push_tone_locked(audio_cw_ms_to_samples(ms));
    audio_cw_unlock();
    return result;
}
mini_result_t tone_stream_hold(bool active)
{
    if (!audio_cw_lock(20)) return MINI_ERR_TIMEOUT;
    if (active != s_hold_active) {
        audio_cw_seg_clear();
        audio_cw_busy_clear();
        if (active) {
            audio_seg_t seg = {.kind = SEG_TONE_HOLD, .attack = true};
            audio_cw_seg_push(&seg);
        }
        s_hold_active = active;
        s_flush_requested = true;
    }
    audio_cw_unlock();
    return MINI_OK;
}
mini_result_t tone_stream_stop(void)
{
    if (!audio_cw_lock(20)) return MINI_ERR_TIMEOUT;
    audio_cw_preempt_locked();
    audio_cw_unlock();
    return MINI_OK;
}
bool tone_stream_busy(void)
{
    s_port.busy_enter();
    bool result = s_keyed_outstanding != 0;
    s_port.busy_exit();
    /* Hold is changed only under the segment mutex. */
    if (audio_cw_lock(UINT32_MAX)) {
        result = result || s_hold_active;
        audio_cw_unlock();
    }
    return result;
}

#include "sidetone.h"

#include <stddef.h>

#define SIDETONE_AMPLITUDE 12000
#define SIDETONE_WRITE_TIMEOUT_MS 20u

/* 32-sample sine table at full sidetone amplitude. Integer DDS keeps the
 * runtime ELF self-contained and avoids a libm dependency. */
static const int16_t SINE_32[32] = {
    0, 2341, 4592, 6667, 8485, 9978, 11087, 11769,
    12000, 11769, 11087, 9978, 8485, 6667, 4592, 2341,
    0, -2341, -4592, -6667, -8485, -9978, -11087, -11769,
    -12000, -11769, -11087, -9978, -8485, -6667, -4592, -2341,
};

/* Mini-CW's 5 ms raised-cosine law, in Q15 with unity = 32768.
 * Entries are round(32768 * (1 - cos(pi * i / 60)) / 2).
 * Interpolation every four samples spans 240 samples without runtime libm.
 * Keeping a position rather than resetting gain preserves continuity if a
 * short press/cancellation reverses an unfinished edge. */
static const uint16_t RAISED_COSINE[61] = {
    0, 22, 90, 202, 358, 558, 802, 1088, 1416, 1786,
    2195, 2643, 3129, 3651, 4208, 4799, 5421, 6073, 6754, 7461,
    8192, 8946, 9720, 10512, 11321, 12144, 12978, 13821, 14671, 15527,
    16384, 17241, 18097, 18947, 19790, 20624, 21447, 22256, 23048, 23822,
    24576, 25307, 26014, 26695, 27347, 27969, 28560, 29117, 29639, 30125,
    30573, 30982, 31352, 31680, 31966, 32210, 32410, 32566, 32678, 32746,
    32768,
};

static uint32_t envelope_gain(uint16_t position)
{
    unsigned index = position / 4u;
    unsigned fraction = position % 4u;
    if (index == 60u) return RAISED_COSINE[60];
    return RAISED_COSINE[index] +
           ((RAISED_COSINE[index + 1u] - RAISED_COSINE[index]) * fraction + 2u) / 4u;
}

static void clear_sidetone(sidetone_t *sidetone)
{
    if (sidetone == NULL) return;
    sidetone->tx = NULL;
    sidetone->stream = MINI_AUDIO_STREAM_INVALID;
    sidetone->phase_q16 = 0u;
    sidetone->phase_step_q16 = 0u;
    sidetone->envelope_pos = 0u;
    sidetone->streaming = false;
    sidetone->volume = 99u;
    sidetone->mute = false;
}

static uint32_t phase_step_for_hz(uint32_t pitch_hz)
{
    /* pitch * 65536 / 48000. 5592 / 4096 is within 0.01% of the exact ratio
     * and uses only integer multiply/shift operations in the external ELF. */
    return (pitch_hz * 5592u + 2048u) >> 12;
}

static mini_result_t write_block(sidetone_t *sidetone, bool key_down)
{
    int16_t frames[KEYER_SIDETONE_BLOCK_FRAMES];
    uint32_t sent = 0u;

    for (uint32_t i = 0u; i < KEYER_SIDETONE_BLOCK_FRAMES; ++i) {
        if (key_down) {
            if (sidetone->envelope_pos < KEYER_SIDETONE_EDGE_SAMPLES) ++sidetone->envelope_pos;
        } else if (sidetone->envelope_pos > 0u) {
            --sidetone->envelope_pos;
        }

        uint32_t index = (sidetone->phase_q16 >> 11) & 31u;
        int32_t sample = (int32_t)SINE_32[index] * (int32_t)envelope_gain(sidetone->envelope_pos);
        sample = (sample >> 15) * sidetone->volume / 99;
        frames[i] = sidetone->mute ? 0 : (int16_t)sample;
        sidetone->phase_q16 = (sidetone->phase_q16 + sidetone->phase_step_q16) & 0xffffu;
    }

    while (sent < KEYER_SIDETONE_BLOCK_FRAMES) {
        uint32_t written = 0u;
        mini_result_t rc = sidetone->tx->write(
            sidetone->stream,
            &frames[sent],
            KEYER_SIDETONE_BLOCK_FRAMES - sent,
            &written,
            SIDETONE_WRITE_TIMEOUT_MS);
        if (rc != MINI_OK) return rc;
        if (written == 0u) return MINI_ERR_IO;
        sent += written;
    }

    return MINI_OK;
}

mini_result_t sidetone_open(sidetone_t *sidetone,
                            const mini_audio_api_t *audio,
                            bool enabled,
                            uint32_t pitch_hz)
{
    mini_audio_format_t format;
    mini_result_t rc;

    if (sidetone == NULL) return MINI_ERR_INVALID;
    clear_sidetone(sidetone);
    if (!enabled) return MINI_OK;

    if (audio == NULL || (audio->capabilities & MINI_AUDIO_CAP_TX) == 0u ||
        audio->tx == NULL || audio->tx->open == NULL || audio->tx->start == NULL ||
        audio->tx->write == NULL || audio->tx->stop == NULL || audio->tx->close == NULL) {
        return MINI_ERR_UNSUPPORTED;
    }

    sidetone->tx = audio->tx;
    sidetone->phase_step_q16 = phase_step_for_hz(pitch_hz);
    if (sidetone->phase_step_q16 == 0u) {
        clear_sidetone(sidetone);
        return MINI_ERR_INVALID;
    }

    format.struct_size = sizeof(format);
    format.sample_rate_hz = KEYER_SIDETONE_SAMPLE_RATE_HZ;
    format.sample_format = MINI_AUDIO_SAMPLE_S16;
    format.channels = 1u;

    rc = sidetone->tx->open("speaker", &format, &sidetone->stream);
    if (rc != MINI_OK) {
        clear_sidetone(sidetone);
        return rc;
    }

    rc = sidetone->tx->start(sidetone->stream);
    if (rc != MINI_OK) {
        (void)sidetone->tx->close(sidetone->stream);
        clear_sidetone(sidetone);
        return rc;
    }

    sidetone->streaming = true;
    return MINI_OK;
}

mini_result_t sidetone_apply(sidetone_t *sidetone, bool key_down)
{
    if (sidetone == NULL) return MINI_ERR_INVALID;
    if (!sidetone->streaming) return MINI_OK;
    return write_block(sidetone, key_down);
}

bool sidetone_streaming(const sidetone_t *sidetone)
{
    return sidetone != NULL && sidetone->streaming;
}

void sidetone_close(sidetone_t *sidetone)
{
    if (sidetone == NULL || !sidetone->streaming || sidetone->tx == NULL) {
        if (sidetone != NULL) clear_sidetone(sidetone);
        return;
    }

    /* Give an active tone a short click-reducing release before stopping the
     * stream. Five 1 ms blocks complete even a full 240-sample raised-cosine edge. */
    for (uint32_t i = 0u; i < 5u && sidetone->envelope_pos > 0u; ++i) {
        if (write_block(sidetone, false) != MINI_OK) {
            if (sidetone->tx->abort != NULL) (void)sidetone->tx->abort(sidetone->stream);
            (void)sidetone->tx->close(sidetone->stream);
            clear_sidetone(sidetone);
            return;
        }
    }

    (void)sidetone->tx->stop(sidetone->stream);
    (void)sidetone->tx->close(sidetone->stream);
    clear_sidetone(sidetone);
}

void sidetone_settings(sidetone_t *sidetone, uint32_t hz, uint8_t volume, bool mute)
{
    sidetone->phase_step_q16 = phase_step_for_hz(hz);
    sidetone->volume = volume > 99u ? 99u : volume;
    sidetone->mute = mute;
}

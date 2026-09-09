#include "rx_frontend.h"

#include <string.h>

static int config_valid(const RxFrontendConfig *config)
{
    if (config == NULL)
        return 0;

    switch (config->audio_mode) {
    case RX_FRONTEND_AUDIO_AVERAGE:
    case RX_FRONTEND_AUDIO_CHANNEL_0:
    case RX_FRONTEND_AUDIO_CHANNEL_1:
        return 1;
    default:
        return 0;
    }
}

static size_t required_output(size_t frame_count, uint8_t phase)
{
    if (phase == 0u)
        return (frame_count + 1u) / 2u;
    return frame_count / 2u;
}

static float frame_to_mono(const RxFrontend *frontend,
                           const int16_t frame[RX_FRONTEND_INPUT_CHANNELS])
{
    if (frontend->config.audio_mode == RX_FRONTEND_AUDIO_CHANNEL_0)
        return (float)frame[0] / 32768.0f;
    if (frontend->config.audio_mode == RX_FRONTEND_AUDIO_CHANNEL_1)
        return (float)frame[1] / 32768.0f;

    /* Preserve V2 operation ordering for ordinary stereo audio. */
    {
        float mono = 0.0f;
        mono += (float)frame[0] / 32768.0f;
        mono += (float)frame[1] / 32768.0f;
        mono /= 2.0f;
        return mono;
    }
}

RxFrontendConfig rx_frontend_baseline_config(void)
{
    RxFrontendConfig config;
    config.audio_mode = RX_FRONTEND_AUDIO_AVERAGE;
    return config;
}

RxFrontendStatus rx_frontend_init(RxFrontend *frontend,
                                  const RxFrontendConfig *config)
{
    if (frontend == NULL || !config_valid(config))
        return RX_FRONTEND_ERR_INVALID;

    memset(frontend, 0, sizeof(*frontend));
    frontend->config = *config;
    frontend->initialized = 1;
    return RX_FRONTEND_OK;
}

void rx_frontend_reset_stream(RxFrontend *frontend)
{
    if (frontend == NULL || !frontend->initialized)
        return;
    frontend->decimation_phase = 0u;
}

void rx_frontend_destroy(RxFrontend *frontend)
{
    if (frontend == NULL)
        return;
    memset(frontend, 0, sizeof(*frontend));
}

RxFrontendStatus rx_frontend_process(RxFrontend *frontend,
                                     const int16_t *interleaved_s16,
                                     size_t frame_count,
                                     float *out_samples,
                                     size_t out_capacity,
                                     size_t *out_count)
{
    size_t needed;
    size_t produced = 0u;
    size_t i;
    uint8_t phase;

    if (out_count == NULL)
        return RX_FRONTEND_ERR_INVALID;
    *out_count = 0u;

    if (frontend == NULL ||
        (frame_count > 0u && interleaved_s16 == NULL) ||
        (out_capacity > 0u && out_samples == NULL))
        return RX_FRONTEND_ERR_INVALID;
    if (!frontend->initialized)
        return RX_FRONTEND_ERR_NOT_INITIALIZED;

    needed = required_output(frame_count, frontend->decimation_phase);
    if (needed > out_capacity)
        return RX_FRONTEND_ERR_OUTPUT_FULL;

    phase = frontend->decimation_phase;
    for (i = 0u; i < frame_count; ++i) {
        const int16_t *frame = &interleaved_s16[i * RX_FRONTEND_INPUT_CHANNELS];

        if (phase == 0u)
            out_samples[produced++] = frame_to_mono(frontend, frame);
        phase ^= 1u;
    }

    frontend->decimation_phase = phase;
    *out_count = produced;
    return RX_FRONTEND_OK;
}

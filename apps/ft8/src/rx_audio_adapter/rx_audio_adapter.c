#include "rx_audio_adapter.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

#define FIELD_END(type, field) \
    ((uint32_t)(offsetof(type, field) + sizeof(((type *)0)->field)))

static RxAudioAdapterStatus fail(RxAudioAdapter *adapter, mini_result_t result)
{
    if (adapter != NULL)
        adapter->last_result = result;
    return RX_AUDIO_ADAPTER_ERR_MINISHELL;
}

RxAudioAdapterStatus rx_audio_adapter_init(RxAudioAdapter *adapter,
                                           const mini_audio_api_t *audio)
{
    if (adapter == NULL)
        return RX_AUDIO_ADAPTER_ERR_INVALID;

    memset(adapter, 0, sizeof(*adapter));
    adapter->stream = MINI_AUDIO_STREAM_INVALID;
    adapter->last_result = MINI_OK;

    if (audio == NULL ||
        audio->struct_size < FIELD_END(mini_audio_api_t, rx) ||
        (audio->capabilities & MINI_AUDIO_CAP_RX) == 0u ||
        audio->rx == NULL ||
        audio->rx->struct_size < FIELD_END(mini_audio_rx_api_t, close) ||
        audio->rx->open == NULL || audio->rx->start == NULL ||
        audio->rx->read == NULL || audio->rx->stop == NULL ||
        audio->rx->close == NULL) {
        return RX_AUDIO_ADAPTER_ERR_UNAVAILABLE;
    }

    adapter->audio = audio;
    adapter->initialized = 1;
    return RX_AUDIO_ADAPTER_OK;
}

RxAudioAdapterStatus rx_audio_adapter_open(RxAudioAdapter *adapter,
                                           const char *endpoint)
{
    mini_audio_format_t format;
    mini_result_t result;

    if (adapter == NULL || !adapter->initialized)
        return RX_AUDIO_ADAPTER_ERR_STATE;
    if (adapter->open)
        return RX_AUDIO_ADAPTER_ERR_STATE;

    memset(&format, 0, sizeof(format));
    format.struct_size = sizeof(format);
    format.sample_rate_hz = RX_AUDIO_ADAPTER_SAMPLE_RATE_HZ;
    format.sample_format = RX_AUDIO_ADAPTER_SAMPLE_FORMAT;
    format.channels = RX_AUDIO_ADAPTER_CHANNELS;

    result = adapter->audio->rx->open(endpoint, &format, &adapter->stream);
    adapter->last_result = result;
    if (result != MINI_OK)
        return RX_AUDIO_ADAPTER_ERR_MINISHELL;
    if (adapter->stream == MINI_AUDIO_STREAM_INVALID)
        return fail(adapter, MINI_ERR_IO);

    adapter->open = 1;
    return RX_AUDIO_ADAPTER_OK;
}

RxAudioAdapterStatus rx_audio_adapter_start(RxAudioAdapter *adapter)
{
    mini_result_t result;

    if (adapter == NULL || !adapter->initialized || !adapter->open)
        return RX_AUDIO_ADAPTER_ERR_STATE;
    if (adapter->started)
        return RX_AUDIO_ADAPTER_OK;

    result = adapter->audio->rx->start(adapter->stream);
    adapter->last_result = result;
    if (result != MINI_OK)
        return RX_AUDIO_ADAPTER_ERR_MINISHELL;

    adapter->started = 1;
    return RX_AUDIO_ADAPTER_OK;
}

RxAudioAdapterStatus rx_audio_adapter_read(RxAudioAdapter *adapter,
                                           int16_t *interleaved_s16,
                                           size_t frame_capacity,
                                           size_t *out_frames,
                                           uint32_t timeout_ms)
{
    uint32_t got = 0u;
    mini_result_t result;

    if (out_frames == NULL)
        return RX_AUDIO_ADAPTER_ERR_INVALID;
    *out_frames = 0u;

    if (adapter == NULL || !adapter->initialized || !adapter->open ||
        !adapter->started)
        return RX_AUDIO_ADAPTER_ERR_STATE;
    if (frame_capacity > UINT32_MAX)
        return RX_AUDIO_ADAPTER_ERR_INVALID;
    if (frame_capacity > 0u && interleaved_s16 == NULL)
        return RX_AUDIO_ADAPTER_ERR_INVALID;

    result = adapter->audio->rx->read(adapter->stream, interleaved_s16,
                                      (uint32_t)frame_capacity, &got,
                                      timeout_ms);
    adapter->last_result = result;
    if (result == MINI_ERR_END_OF_STREAM)
        return RX_AUDIO_ADAPTER_END_OF_STREAM;
    if (result != MINI_OK)
        return RX_AUDIO_ADAPTER_ERR_MINISHELL;
    if ((size_t)got > frame_capacity)
        return fail(adapter, MINI_ERR_IO);

    *out_frames = (size_t)got;
    return RX_AUDIO_ADAPTER_OK;
}

RxAudioAdapterStatus rx_audio_adapter_stop(RxAudioAdapter *adapter)
{
    mini_result_t result;

    if (adapter == NULL || !adapter->initialized || !adapter->open)
        return RX_AUDIO_ADAPTER_ERR_STATE;
    if (!adapter->started)
        return RX_AUDIO_ADAPTER_OK;

    result = adapter->audio->rx->stop(adapter->stream);
    adapter->last_result = result;
    if (result != MINI_OK)
        return RX_AUDIO_ADAPTER_ERR_MINISHELL;

    adapter->started = 0;
    return RX_AUDIO_ADAPTER_OK;
}

RxAudioAdapterStatus rx_audio_adapter_close(RxAudioAdapter *adapter)
{
    mini_result_t first_error = MINI_OK;
    mini_result_t result;

    if (adapter == NULL || !adapter->initialized)
        return RX_AUDIO_ADAPTER_ERR_STATE;
    if (!adapter->open)
        return RX_AUDIO_ADAPTER_OK;

    if (adapter->started) {
        result = adapter->audio->rx->stop(adapter->stream);
        if (result == MINI_OK) {
            adapter->started = 0;
        } else {
            first_error = result;
        }
    }

    result = adapter->audio->rx->close(adapter->stream);
    if (result == MINI_OK) {
        adapter->stream = MINI_AUDIO_STREAM_INVALID;
        adapter->open = 0;
        adapter->started = 0;
    } else if (first_error == MINI_OK) {
        first_error = result;
    }

    adapter->last_result = first_error;
    return first_error == MINI_OK ? RX_AUDIO_ADAPTER_OK
                                  : RX_AUDIO_ADAPTER_ERR_MINISHELL;
}

mini_result_t rx_audio_adapter_last_result(const RxAudioAdapter *adapter)
{
    return adapter != NULL ? adapter->last_result : MINI_ERR_INVALID;
}

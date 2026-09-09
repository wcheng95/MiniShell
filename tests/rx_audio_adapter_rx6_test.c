#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "rx_audio_adapter.h"

static mini_audio_stream_t s_stream = 7u;
static int s_open_count;
static int s_start_count;
static int s_read_count;
static int s_stop_count;
static int s_close_count;
static mini_audio_format_t s_format;
static mini_result_t s_read_result = MINI_OK;

static mini_result_t fake_open(const char *endpoint, const mini_audio_format_t *format,
                               mini_audio_stream_t *out_stream)
{
    assert(endpoint != NULL && strcmp(endpoint, "/flash/test.wav") == 0);
    s_format = *format;
    *out_stream = s_stream;
    ++s_open_count;
    return MINI_OK;
}

static mini_result_t fake_start(mini_audio_stream_t stream)
{
    assert(stream == s_stream);
    ++s_start_count;
    return MINI_OK;
}

static mini_result_t fake_read(mini_audio_stream_t stream, void *frames,
                               uint32_t capacity, uint32_t *out_frames,
                               uint32_t timeout_ms)
{
    int16_t *samples = (int16_t *)frames;
    assert(stream == s_stream);
    assert(capacity >= 2u);
    assert(timeout_ms == 123u);
    ++s_read_count;
    if (s_read_result != MINI_OK) {
        *out_frames = 0u;
        return s_read_result;
    }
    samples[0] = 100;
    samples[1] = -100;
    samples[2] = 200;
    samples[3] = -200;
    *out_frames = 2u;
    return MINI_OK;
}

static mini_result_t fake_stop(mini_audio_stream_t stream)
{
    assert(stream == s_stream);
    ++s_stop_count;
    return MINI_OK;
}

static mini_result_t fake_close(mini_audio_stream_t stream)
{
    assert(stream == s_stream);
    ++s_close_count;
    return MINI_OK;
}

int main(void)
{
    mini_audio_rx_api_t rx = {
        .struct_size = sizeof(rx),
        .open = fake_open,
        .start = fake_start,
        .read = fake_read,
        .stop = fake_stop,
        .close = fake_close,
    };
    mini_audio_api_t audio = {
        .struct_size = sizeof(audio),
        .capabilities = MINI_AUDIO_CAP_RX,
        .rx = &rx,
        .tx = NULL,
    };
    RxAudioAdapter adapter;
    int16_t frames[8];
    size_t got = 99u;

    assert(rx_audio_adapter_init(NULL, &audio) == RX_AUDIO_ADAPTER_ERR_INVALID);
    assert(rx_audio_adapter_init(&adapter, NULL) == RX_AUDIO_ADAPTER_ERR_UNAVAILABLE);
    assert(rx_audio_adapter_init(&adapter, &audio) == RX_AUDIO_ADAPTER_OK);
    assert(rx_audio_adapter_read(&adapter, frames, 4u, &got, 123u) == RX_AUDIO_ADAPTER_ERR_STATE);

    assert(rx_audio_adapter_open(&adapter, "/flash/test.wav") == RX_AUDIO_ADAPTER_OK);
    assert(s_open_count == 1);
    assert(s_format.sample_rate_hz == RX_AUDIO_ADAPTER_SAMPLE_RATE_HZ);
    assert(s_format.sample_format == MINI_AUDIO_SAMPLE_S16);
    assert(s_format.channels == RX_AUDIO_ADAPTER_CHANNELS);

    assert(rx_audio_adapter_start(&adapter) == RX_AUDIO_ADAPTER_OK);
    assert(s_start_count == 1);
    assert(rx_audio_adapter_read(&adapter, frames, 4u, &got, 123u) == RX_AUDIO_ADAPTER_OK);
    assert(got == 2u && frames[0] == 100 && frames[3] == -200);

    s_read_result = MINI_ERR_END_OF_STREAM;
    got = 99u;
    assert(rx_audio_adapter_read(&adapter, frames, 4u, &got, 123u) == RX_AUDIO_ADAPTER_END_OF_STREAM);
    assert(got == 0u);
    assert(rx_audio_adapter_last_result(&adapter) == MINI_ERR_END_OF_STREAM);

    /* close() owns cleanup even when caller did not stop explicitly. */
    assert(rx_audio_adapter_close(&adapter) == RX_AUDIO_ADAPTER_OK);
    assert(s_stop_count == 1 && s_close_count == 1);
    assert(adapter.stream == MINI_AUDIO_STREAM_INVALID && !adapter.open && !adapter.started);
    assert(rx_audio_adapter_close(&adapter) == RX_AUDIO_ADAPTER_OK);

    return 0;
}

#include "test_support.h"

#define FAKE_RX_HANDLE ((minishell_backend_audio_t)101u)
#define FAKE_TX_HANDLE ((minishell_backend_audio_t)202u)

typedef struct {
    uint32_t rx_open_calls;
    uint32_t rx_start_calls;
    uint32_t rx_read_calls;
    uint32_t rx_stop_calls;
    uint32_t rx_close_calls;
    uint32_t rx_pos;
    bool rx_open;
    bool rx_started;
    char rx_endpoint[64];

    uint32_t tx_open_calls;
    uint32_t tx_start_calls;
    uint32_t tx_write_calls;
    uint32_t tx_stop_calls;
    uint32_t tx_abort_calls;
    uint32_t tx_close_calls;
    bool tx_open;
    bool tx_started;
    char tx_endpoint[64];
    int16_t tx_frames[32];
    uint32_t tx_frame_count;
} fake_audio_state_t;

static fake_audio_state_t s_audio;

static const int16_t s_rx_frames[] = {
    100, -100,
    200, -200,
    300, -300,
};

static void audio_fake_reset(void)
{
    memset(&s_audio, 0, sizeof(s_audio));
}

static mini_result_t require_v1_format(uint32_t sample_rate_hz,
                                       uint32_t sample_format,
                                       uint32_t channels)
{
    if (sample_rate_hz != 12000u || sample_format != MINI_AUDIO_SAMPLE_S16 ||
        channels != 2u) {
        return MINI_ERR_UNSUPPORTED;
    }
    return MINI_OK;
}

static mini_result_t fake_rx_open(void *ctx, const char *endpoint,
                                  uint32_t sample_rate_hz, uint32_t sample_format,
                                  uint32_t channels,
                                  minishell_backend_audio_t *out_audio)
{
    (void)ctx;
    ++s_audio.rx_open_calls;
    mini_result_t result = require_v1_format(sample_rate_hz, sample_format, channels);
    if (result != MINI_OK) return result;
    if (s_audio.rx_open) return MINI_ERR_TOO_MANY_OPEN;
    s_audio.rx_open = true;
    s_audio.rx_pos = 0u;
    snprintf(s_audio.rx_endpoint, sizeof(s_audio.rx_endpoint), "%s",
             endpoint != NULL ? endpoint : "default");
    *out_audio = FAKE_RX_HANDLE;
    return MINI_OK;
}

static mini_result_t fake_rx_start(void *ctx, minishell_backend_audio_t audio)
{
    (void)ctx;
    if (!s_audio.rx_open || audio != FAKE_RX_HANDLE) return MINI_ERR_BAD_HANDLE;
    ++s_audio.rx_start_calls;
    s_audio.rx_started = true;
    return MINI_OK;
}

static mini_result_t fake_rx_read(void *ctx, minishell_backend_audio_t audio,
                                  void *frames, uint32_t frame_capacity,
                                  uint32_t *out_frames, uint32_t timeout_ms)
{
    (void)ctx;
    (void)timeout_ms;
    if (!s_audio.rx_open || audio != FAKE_RX_HANDLE) return MINI_ERR_BAD_HANDLE;
    if (!s_audio.rx_started) return MINI_ERR_NOT_READY;
    ++s_audio.rx_read_calls;

    const uint32_t source_frames = 3u;
    if (s_audio.rx_pos >= source_frames) {
        *out_frames = 0u;
        return MINI_ERR_END_OF_STREAM;
    }

    uint32_t remaining = source_frames - s_audio.rx_pos;
    uint32_t count = frame_capacity < remaining ? frame_capacity : remaining;
    memcpy(frames, &s_rx_frames[s_audio.rx_pos * 2u],
           (size_t)count * 2u * sizeof(int16_t));
    s_audio.rx_pos += count;
    *out_frames = count;
    return MINI_OK;
}

static mini_result_t fake_rx_stop(void *ctx, minishell_backend_audio_t audio)
{
    (void)ctx;
    if (!s_audio.rx_open || audio != FAKE_RX_HANDLE) return MINI_ERR_BAD_HANDLE;
    ++s_audio.rx_stop_calls;
    s_audio.rx_started = false;
    return MINI_OK;
}

static mini_result_t fake_rx_close(void *ctx, minishell_backend_audio_t audio)
{
    (void)ctx;
    if (!s_audio.rx_open || audio != FAKE_RX_HANDLE) return MINI_ERR_BAD_HANDLE;
    ++s_audio.rx_close_calls;
    s_audio.rx_open = false;
    s_audio.rx_started = false;
    return MINI_OK;
}

static mini_result_t fake_tx_open(void *ctx, const char *endpoint,
                                  uint32_t sample_rate_hz, uint32_t sample_format,
                                  uint32_t channels,
                                  minishell_backend_audio_t *out_audio)
{
    (void)ctx;
    ++s_audio.tx_open_calls;
    mini_result_t result = require_v1_format(sample_rate_hz, sample_format, channels);
    if (result != MINI_OK) return result;
    if (s_audio.tx_open) return MINI_ERR_TOO_MANY_OPEN;
    s_audio.tx_open = true;
    s_audio.tx_frame_count = 0u;
    snprintf(s_audio.tx_endpoint, sizeof(s_audio.tx_endpoint), "%s",
             endpoint != NULL ? endpoint : "default");
    *out_audio = FAKE_TX_HANDLE;
    return MINI_OK;
}

static mini_result_t fake_tx_start(void *ctx, minishell_backend_audio_t audio)
{
    (void)ctx;
    if (!s_audio.tx_open || audio != FAKE_TX_HANDLE) return MINI_ERR_BAD_HANDLE;
    ++s_audio.tx_start_calls;
    s_audio.tx_started = true;
    return MINI_OK;
}

static mini_result_t fake_tx_write(void *ctx, minishell_backend_audio_t audio,
                                   const void *frames, uint32_t frame_count,
                                   uint32_t *out_frames, uint32_t timeout_ms)
{
    (void)ctx;
    (void)timeout_ms;
    if (!s_audio.tx_open || audio != FAKE_TX_HANDLE) return MINI_ERR_BAD_HANDLE;
    if (!s_audio.tx_started) return MINI_ERR_NOT_READY;
    ++s_audio.tx_write_calls;
    if (s_audio.tx_frame_count + frame_count > 16u) return MINI_ERR_NO_SPACE;

    memcpy(&s_audio.tx_frames[s_audio.tx_frame_count * 2u], frames,
           (size_t)frame_count * 2u * sizeof(int16_t));
    s_audio.tx_frame_count += frame_count;
    *out_frames = frame_count;
    return MINI_OK;
}

static mini_result_t fake_tx_stop(void *ctx, minishell_backend_audio_t audio)
{
    (void)ctx;
    if (!s_audio.tx_open || audio != FAKE_TX_HANDLE) return MINI_ERR_BAD_HANDLE;
    ++s_audio.tx_stop_calls;
    s_audio.tx_started = false;
    return MINI_OK;
}

static mini_result_t fake_tx_abort(void *ctx, minishell_backend_audio_t audio)
{
    (void)ctx;
    if (!s_audio.tx_open || audio != FAKE_TX_HANDLE) return MINI_ERR_BAD_HANDLE;
    ++s_audio.tx_abort_calls;
    s_audio.tx_started = false;
    return MINI_OK;
}

static mini_result_t fake_tx_close(void *ctx, minishell_backend_audio_t audio)
{
    (void)ctx;
    if (!s_audio.tx_open || audio != FAKE_TX_HANDLE) return MINI_ERR_BAD_HANDLE;
    ++s_audio.tx_close_calls;
    s_audio.tx_open = false;
    s_audio.tx_started = false;
    return MINI_OK;
}

static minishell_services_port_t audio_port(uint64_t capabilities)
{
    minishell_services_port_t port = fake_full_port();
    port.audio_capabilities = capabilities;
    port.audio_rx_open = fake_rx_open;
    port.audio_rx_start = fake_rx_start;
    port.audio_rx_read = fake_rx_read;
    port.audio_rx_stop = fake_rx_stop;
    port.audio_rx_close = fake_rx_close;
    port.audio_tx_open = fake_tx_open;
    port.audio_tx_start = fake_tx_start;
    port.audio_tx_write = fake_tx_write;
    port.audio_tx_stop = fake_tx_stop;
    port.audio_tx_abort = fake_tx_abort;
    port.audio_tx_close = fake_tx_close;
    return port;
}

bool test_audio(void)
{
    fake_reset();
    audio_fake_reset();
    minishell_services_port_t port = audio_port(MINI_AUDIO_CAP_RX | MINI_AUDIO_CAP_TX);
    minishell_services_configure(&port);

    const mini_audio_api_t *audio = mini_api_get()->audio;
    TEST_CHECK(audio != NULL);
    TEST_CHECK((audio->capabilities & MINI_AUDIO_CAP_RX) != 0u);
    TEST_CHECK((audio->capabilities & MINI_AUDIO_CAP_TX) != 0u);
    TEST_CHECK(audio->rx != NULL);
    TEST_CHECK(audio->tx != NULL);

    mini_audio_format_t format = {
        .struct_size = sizeof(format),
        .sample_rate_hz = 12000u,
        .sample_format = MINI_AUDIO_SAMPLE_S16,
        .channels = 2u,
    };

    mini_audio_format_t too_small = format;
    too_small.struct_size = sizeof(uint32_t);
    mini_audio_stream_t stream = 99u;
    TEST_EQ(audio->rx->open(NULL, &too_small, &stream), MINI_ERR_INVALID);
    TEST_EQ(stream, MINI_AUDIO_STREAM_INVALID);

    mini_audio_format_t unsupported = format;
    unsupported.channels = 1u;
    TEST_EQ(audio->rx->open(NULL, &unsupported, &stream), MINI_ERR_UNSUPPORTED);
    TEST_EQ(stream, MINI_AUDIO_STREAM_INVALID);
    TEST_EQ(audio->rx->open("", &format, &stream), MINI_ERR_INVALID);

    mini_audio_stream_t rx = MINI_AUDIO_STREAM_INVALID;
    TEST_EQ(audio->rx->open("fixture", &format, &rx), MINI_OK);
    TEST_CHECK(rx != MINI_AUDIO_STREAM_INVALID);
    TEST_CHECK(strcmp(s_audio.rx_endpoint, "fixture") == 0);
    TEST_EQ(audio->rx->open("second", &format, &stream), MINI_ERR_TOO_MANY_OPEN);

    int16_t rx_buffer[4] = {0};
    uint32_t frames = 99u;
    TEST_EQ(audio->rx->read(rx, rx_buffer, 2u, &frames, MINI_WAIT_NONE), MINI_ERR_NOT_READY);
    TEST_EQ(frames, 0u);
    TEST_EQ(audio->rx->start(rx), MINI_OK);
    TEST_EQ(audio->rx->start(rx), MINI_OK);
    TEST_EQ(s_audio.rx_start_calls, 1u);

    TEST_EQ(audio->rx->read(rx, rx_buffer, 2u, &frames, MINI_WAIT_NONE), MINI_OK);
    TEST_EQ(frames, 2u);
    TEST_EQ(rx_buffer[0], 100);
    TEST_EQ(rx_buffer[1], -100);
    TEST_EQ(rx_buffer[2], 200);
    TEST_EQ(rx_buffer[3], -200);

    memset(rx_buffer, 0, sizeof(rx_buffer));
    TEST_EQ(audio->rx->read(rx, rx_buffer, 2u, &frames, MINI_WAIT_NONE), MINI_OK);
    TEST_EQ(frames, 1u);
    TEST_EQ(rx_buffer[0], 300);
    TEST_EQ(rx_buffer[1], -300);
    TEST_EQ(audio->rx->read(rx, rx_buffer, 2u, &frames, MINI_WAIT_NONE),
            MINI_ERR_END_OF_STREAM);
    TEST_EQ(frames, 0u);

    TEST_EQ(audio->rx->stop(rx), MINI_OK);
    TEST_EQ(audio->rx->stop(rx), MINI_OK);
    TEST_EQ(s_audio.rx_stop_calls, 1u);
    TEST_EQ(audio->rx->close(rx), MINI_OK);
    TEST_EQ(s_audio.rx_close_calls, 1u);
    TEST_EQ(audio->rx->start(rx), MINI_ERR_BAD_HANDLE);

    mini_audio_stream_t tx = MINI_AUDIO_STREAM_INVALID;
    TEST_EQ(audio->tx->open("sink", &format, &tx), MINI_OK);
    TEST_CHECK(tx != MINI_AUDIO_STREAM_INVALID);
    TEST_CHECK(strcmp(s_audio.tx_endpoint, "sink") == 0);
    TEST_EQ(audio->tx->start(tx), MINI_OK);

    const int16_t tx_source[] = {10, 20, 30, 40};
    frames = 0u;
    TEST_EQ(audio->tx->write(tx, tx_source, 2u, &frames, MINI_WAIT_NONE), MINI_OK);
    TEST_EQ(frames, 2u);
    TEST_EQ(s_audio.tx_frame_count, 2u);
    TEST_EQ(s_audio.tx_frames[0], 10);
    TEST_EQ(s_audio.tx_frames[1], 20);
    TEST_EQ(s_audio.tx_frames[2], 30);
    TEST_EQ(s_audio.tx_frames[3], 40);

    TEST_EQ(audio->tx->abort(tx), MINI_OK);
    TEST_EQ(s_audio.tx_abort_calls, 1u);
    TEST_EQ(audio->tx->write(tx, tx_source, 1u, &frames, MINI_WAIT_NONE),
            MINI_ERR_NOT_READY);
    TEST_EQ(audio->tx->start(tx), MINI_OK);
    TEST_EQ(audio->tx->stop(tx), MINI_OK);
    TEST_EQ(audio->tx->close(tx), MINI_OK);
    TEST_EQ(s_audio.tx_close_calls, 1u);

    /* RX and TX can be active independently and app_end performs fail-safe cleanup. */
    TEST_EQ(audio->rx->open(NULL, &format, &rx), MINI_OK);
    TEST_EQ(audio->tx->open(NULL, &format, &tx), MINI_OK);
    TEST_CHECK(rx != tx);
    TEST_EQ(audio->rx->start(rx), MINI_OK);
    TEST_EQ(audio->tx->start(tx), MINI_OK);
    uint32_t rx_stop_before = s_audio.rx_stop_calls;
    uint32_t rx_close_before = s_audio.rx_close_calls;
    uint32_t tx_abort_before = s_audio.tx_abort_calls;
    uint32_t tx_close_before = s_audio.tx_close_calls;
    minishell_services_app_end();
    TEST_EQ(s_audio.rx_stop_calls, rx_stop_before + 1u);
    TEST_EQ(s_audio.rx_close_calls, rx_close_before + 1u);
    TEST_EQ(s_audio.tx_abort_calls, tx_abort_before + 1u);
    TEST_EQ(s_audio.tx_close_calls, tx_close_before + 1u);
    TEST_EQ(audio->rx->start(rx), MINI_ERR_BAD_HANDLE);
    TEST_EQ(audio->tx->start(tx), MINI_ERR_BAD_HANDLE);

    /* Capability bits and mandatory callback presence control exposure. */
    fake_reset();
    audio_fake_reset();
    port = audio_port(MINI_AUDIO_CAP_RX);
    minishell_services_configure(&port);
    audio = mini_api_get()->audio;
    TEST_CHECK(audio != NULL);
    TEST_CHECK(audio->rx != NULL);
    TEST_CHECK(audio->tx == NULL);
    TEST_EQ(audio->capabilities, MINI_AUDIO_CAP_RX);

    fake_reset();
    audio_fake_reset();
    port = audio_port(MINI_AUDIO_CAP_RX);
    port.audio_rx_read = NULL;
    minishell_services_configure(&port);
    TEST_CHECK(mini_api_get()->audio == NULL);

    fake_reset();
    minishell_services_port_t no_audio = fake_full_port();
    minishell_services_configure(&no_audio);
    TEST_CHECK(mini_api_get()->audio == NULL);

    return true;
}

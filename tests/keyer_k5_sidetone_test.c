#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sidetone.h"

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        exit(1); \
    } \
} while (0)

static uint32_t s_open_calls;
static uint32_t s_start_calls;
static uint32_t s_write_calls;
static uint32_t s_stop_calls;
static uint32_t s_abort_calls;
static uint32_t s_close_calls;
static uint32_t s_nonzero_this_apply;
static char s_endpoint[16];
static mini_audio_format_t s_format;

static mini_result_t fake_open(const char *endpoint, const mini_audio_format_t *format,
                               mini_audio_stream_t *out_stream)
{
    ++s_open_calls;
    CHECK(endpoint != NULL);
    CHECK(format != NULL);
    CHECK(out_stream != NULL);
    snprintf(s_endpoint, sizeof(s_endpoint), "%s", endpoint);
    s_format = *format;
    *out_stream = 7u;
    return MINI_OK;
}

static mini_result_t fake_start(mini_audio_stream_t stream)
{
    CHECK(stream == 7u);
    ++s_start_calls;
    return MINI_OK;
}

static mini_result_t fake_write(mini_audio_stream_t stream, const void *frames,
                                uint32_t frame_count, uint32_t *out_frames,
                                uint32_t timeout_ms)
{
    CHECK(stream == 7u);
    CHECK(frames != NULL);
    CHECK(out_frames != NULL);
    CHECK(timeout_ms > 0u);
    ++s_write_calls;

    /* Deliberately accept partial writes so sidetone_apply() must finish the block. */
    uint32_t accepted = frame_count > 17u ? 17u : frame_count;
    const int16_t *samples = (const int16_t *)frames;
    for (uint32_t i = 0u; i < accepted; ++i) {
        if (samples[i] != 0) ++s_nonzero_this_apply;
    }
    *out_frames = accepted;
    return MINI_OK;
}

static mini_result_t fake_stop(mini_audio_stream_t stream)
{
    CHECK(stream == 7u);
    ++s_stop_calls;
    return MINI_OK;
}

static mini_result_t fake_abort(mini_audio_stream_t stream)
{
    CHECK(stream == 7u);
    ++s_abort_calls;
    return MINI_OK;
}

static mini_result_t fake_close(mini_audio_stream_t stream)
{
    CHECK(stream == 7u);
    ++s_close_calls;
    return MINI_OK;
}

static const mini_audio_tx_api_t TX = {
    .struct_size = sizeof(mini_audio_tx_api_t),
    .open = fake_open,
    .start = fake_start,
    .write = fake_write,
    .stop = fake_stop,
    .abort = fake_abort,
    .close = fake_close,
};

static const mini_audio_api_t AUDIO = {
    .struct_size = sizeof(mini_audio_api_t),
    .capabilities = MINI_AUDIO_CAP_TX,
    .tx = &TX,
};

static void reset_fake(void)
{
    s_open_calls = 0u;
    s_start_calls = 0u;
    s_write_calls = 0u;
    s_stop_calls = 0u;
    s_abort_calls = 0u;
    s_close_calls = 0u;
    s_nonzero_this_apply = 0u;
    s_endpoint[0] = '\0';
    s_format.struct_size = 0u;
    s_format.sample_rate_hz = 0u;
    s_format.sample_format = 0u;
    s_format.channels = 0u;
}

int main(void)
{
    sidetone_t sidetone;

    reset_fake();
    CHECK(sidetone_open(&sidetone, NULL, false, 700u) == MINI_OK);
    CHECK(!sidetone_streaming(&sidetone));
    CHECK(s_open_calls == 0u);

    CHECK(sidetone_open(&sidetone, NULL, true, 700u) == MINI_ERR_UNSUPPORTED);
    CHECK(!sidetone_streaming(&sidetone));

    CHECK(sidetone_open(&sidetone, &AUDIO, true, 700u) == MINI_OK);
    CHECK(sidetone_streaming(&sidetone));
    CHECK(s_open_calls == 1u);
    CHECK(s_start_calls == 1u);
    CHECK(strcmp(s_endpoint, "speaker") == 0);
    CHECK(s_format.sample_rate_hz == KEYER_SIDETONE_SAMPLE_RATE_HZ);
    CHECK(s_format.sample_format == MINI_AUDIO_SAMPLE_S16);
    CHECK(s_format.channels == 1u);

    s_nonzero_this_apply = 0u;
    CHECK(sidetone_apply(&sidetone, true) == MINI_OK);
    CHECK(s_nonzero_this_apply > 0u);
    CHECK(s_write_calls >= 3u);  /* 48 frames completed through partial writes. */

    for (uint32_t i = 0u; i < 5u; ++i) {
        CHECK(sidetone_apply(&sidetone, true) == MINI_OK);
    }

    for (uint32_t i = 0u; i < 6u; ++i) {
        CHECK(sidetone_apply(&sidetone, false) == MINI_OK);
    }
    CHECK(sidetone.gain_q8 == 0u);

    /* Gain reaches zero during the sixth release block; the following block
     * must therefore be pure silence. */
    s_nonzero_this_apply = 0u;
    CHECK(sidetone_apply(&sidetone, false) == MINI_OK);
    CHECK(s_nonzero_this_apply == 0u);

    sidetone_close(&sidetone);
    CHECK(!sidetone_streaming(&sidetone));
    CHECK(s_stop_calls == 1u);
    CHECK(s_abort_calls == 0u);
    CHECK(s_close_calls == 1u);

    puts("keyer_k5_sidetone_test: PASS");
    return 0;
}

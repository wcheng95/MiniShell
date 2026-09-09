#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "rx_frontend.h"

static void test_baseline_average_and_decimation(void)
{
    const int16_t frames[] = {
        16384, 0,
        3000, 5000,
        -16384, -16384,
        1000, -1000,
        8192, 8192,
    };
    RxFrontend frontend;
    RxFrontendConfig config = rx_frontend_baseline_config();
    float out[3] = {0};
    size_t count = 0u;

    assert(rx_frontend_init(&frontend, &config) == RX_FRONTEND_OK);
    assert(rx_frontend_process(&frontend, frames, 5u, out, 3u, &count) == RX_FRONTEND_OK);
    assert(count == 3u);
    assert(out[0] == 0.25f);
    assert(out[1] == -0.5f);
    assert(out[2] == 0.25f);
    rx_frontend_destroy(&frontend);
}

static void test_transport_boundaries_are_invisible(void)
{
    const int16_t frames[] = {
        4096, 4096,
        8192, 8192,
        12288, 12288,
        16384, 16384,
        20480, 20480,
        24576, 24576,
    };
    RxFrontend a;
    RxFrontend b;
    RxFrontendConfig config = rx_frontend_baseline_config();
    float whole[3] = {0};
    float split[3] = {0};
    size_t whole_count = 0u;
    size_t count0 = 0u;
    size_t count1 = 0u;

    assert(rx_frontend_init(&a, &config) == RX_FRONTEND_OK);
    assert(rx_frontend_init(&b, &config) == RX_FRONTEND_OK);

    assert(rx_frontend_process(&a, frames, 6u, whole, 3u, &whole_count) == RX_FRONTEND_OK);
    assert(whole_count == 3u);

    assert(rx_frontend_process(&b, frames, 1u, split, 3u, &count0) == RX_FRONTEND_OK);
    assert(count0 == 1u);
    assert(rx_frontend_process(&b, frames + 2u, 5u, split + count0,
                               3u - count0, &count1) == RX_FRONTEND_OK);
    assert(count0 + count1 == whole_count);
    assert(memcmp(whole, split, sizeof(whole)) == 0);

    rx_frontend_destroy(&a);
    rx_frontend_destroy(&b);
}

static void test_channel_modes(void)
{
    const int16_t frames[] = {
        16384, -16384,
        111, 222,
    };
    RxFrontend frontend;
    RxFrontendConfig config = rx_frontend_baseline_config();
    float out = 0.0f;
    size_t count = 0u;

    config.audio_mode = RX_FRONTEND_AUDIO_CHANNEL_0;
    assert(rx_frontend_init(&frontend, &config) == RX_FRONTEND_OK);
    assert(rx_frontend_process(&frontend, frames, 2u, &out, 1u, &count) == RX_FRONTEND_OK);
    assert(count == 1u && out == 0.5f);
    rx_frontend_destroy(&frontend);

    config.audio_mode = RX_FRONTEND_AUDIO_CHANNEL_1;
    assert(rx_frontend_init(&frontend, &config) == RX_FRONTEND_OK);
    assert(rx_frontend_process(&frontend, frames, 2u, &out, 1u, &count) == RX_FRONTEND_OK);
    assert(count == 1u && out == -0.5f);
    rx_frontend_destroy(&frontend);
}

static void test_reset_and_output_full_are_explicit(void)
{
    const int16_t frames[] = {
        4096, 4096,
        8192, 8192,
        12288, 12288,
    };
    RxFrontend frontend;
    RxFrontendConfig config = rx_frontend_baseline_config();
    float out[2] = {0};
    size_t count = 99u;

    assert(rx_frontend_init(&frontend, &config) == RX_FRONTEND_OK);

    assert(rx_frontend_process(&frontend, frames, 3u, out, 1u, &count) == RX_FRONTEND_ERR_OUTPUT_FULL);
    assert(count == 0u);
    assert(frontend.decimation_phase == 0u);

    assert(rx_frontend_process(&frontend, frames, 1u, out, 1u, &count) == RX_FRONTEND_OK);
    assert(count == 1u);
    assert(frontend.decimation_phase == 1u);

    rx_frontend_reset_stream(&frontend);
    assert(frontend.decimation_phase == 0u);
    assert(rx_frontend_process(&frontend, frames + 2u, 1u, out, 1u, &count) == RX_FRONTEND_OK);
    assert(count == 1u);
    assert(out[0] == 0.25f);

    rx_frontend_destroy(&frontend);
}

static void test_invalid_inputs(void)
{
    RxFrontend frontend;
    RxFrontendConfig config = rx_frontend_baseline_config();
    float out = 0.0f;
    size_t count = 0u;

    memset(&frontend, 0, sizeof(frontend));
    assert(rx_frontend_process(&frontend, NULL, 0u, NULL, 0u, &count) ==
           RX_FRONTEND_ERR_NOT_INITIALIZED);

    config.audio_mode = (RxFrontendAudioMode)99;
    assert(rx_frontend_init(&frontend, &config) == RX_FRONTEND_ERR_INVALID);

    config = rx_frontend_baseline_config();
    assert(rx_frontend_init(&frontend, &config) == RX_FRONTEND_OK);
    assert(rx_frontend_process(&frontend, NULL, 1u, &out, 1u, &count) ==
           RX_FRONTEND_ERR_INVALID);
    assert(rx_frontend_process(&frontend, NULL, 0u, NULL, 0u, &count) ==
           RX_FRONTEND_OK);
    assert(count == 0u);
    rx_frontend_destroy(&frontend);
}

int main(void)
{
    test_baseline_average_and_decimation();
    test_transport_boundaries_are_invisible();
    test_channel_modes();
    test_reset_and_output_full_are_explicit();
    test_invalid_inputs();
    puts("rx_frontend_rx3_test: PASS");
    return 0;
}

#include <stdio.h>
#include <stdlib.h>
#include "adv_audio_tx_write.h"

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%d: %s\n", __LINE__, #x); exit(1); } } while (0)
#define NATIVE_TIMEOUT 0x107
static int native_result;
static size_t native_written;
static uint32_t expected_timeout;
static unsigned calls;
static int16_t samples[48];
static int fake_handle;

static int write_fake(void *handle, const void *frames, size_t bytes,
                       size_t *written, uint32_t timeout_ms)
{
    CHECK(handle == &fake_handle && frames == samples);
    CHECK(bytes == 96 && *written == 0 && timeout_ms == expected_timeout);
    ++calls;
    *written = native_written;
    return native_result;
}
int main(void)
{
    const struct { int result; size_t bytes; mini_result_t expected; uint32_t frames; } cases[] = {
        {0, 96, MINI_OK, 48}, {0, 34, MINI_OK, 17},
        {NATIVE_TIMEOUT, 0, MINI_ERR_TIMEOUT, 0}, {NATIVE_TIMEOUT, 34, MINI_OK, 17},
        {-1, 0, MINI_ERR_IO, 0}, {-1, 34, MINI_OK, 17},
        {0, 33, MINI_ERR_IO, 0}, {NATIVE_TIMEOUT, 33, MINI_ERR_IO, 0},
        {0, 0, MINI_ERR_IO, 0}, {0, 98, MINI_ERR_IO, 0}
    };
    const uint32_t timeouts[] = {20, MINI_WAIT_NONE, MINI_WAIT_FOREVER, 7};
    for (unsigned t = 0; t < sizeof(timeouts) / sizeof(timeouts[0]); ++t) {
        expected_timeout = timeouts[t];
        for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
            uint32_t frames = 999;
            native_result = cases[i].result;
            native_written = cases[i].bytes;
            unsigned before = calls;
            CHECK(adv_audio_tx_write(write_fake, &fake_handle, samples, 48, &frames,
                                     expected_timeout, NATIVE_TIMEOUT) == cases[i].expected);
            CHECK(frames == cases[i].frames && calls == before + 1);
        }
    }
    puts("ADV Audio TX forwarding/progress: PASS (40 cases)");
    return 0;
}

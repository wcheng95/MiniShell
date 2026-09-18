#include <stdbool.h>
#include <stddef.h>
#include "minishell/api.h"

#define BLOCK_FRAMES 48u
#define WARMUP_CALLS 100u
#define MEASURE_CALLS 5000u

/* Explicit integer division keeps the external ELF free of libgcc imports. */
static uint64_t divide(uint64_t value, uint32_t divisor, uint32_t *remainder)
{
    uint64_t quotient = 0;
    uint32_t rem = 0;
    for (int bit = 63; bit >= 0; --bit) {
        rem = (rem << 1) | (uint32_t)((value >> bit) & 1u);
        if (rem >= divisor) {
            rem -= divisor;
            quotient |= UINT64_C(1) << bit;
        }
    }
    *remainder = rem;
    return quotient;
}
static void number(const mini_api_t *api, const char *label, uint64_t value)
{
    char digits[21];
    unsigned pos = sizeof(digits) - 1u;
    digits[pos] = '\0';
    do {
        uint32_t rem;
        value = divide(value, 10, &rem);
        digits[--pos] = (char)('0' + rem);
    } while (value);
    api->console->write(label);
    api->console->write(digits + pos);
    api->console->write("\n");
}
typedef struct {
    uint64_t sum, min, max, frames;
    uint32_t calls, ok, timeout, other, partial, zero;
    uint32_t over[5];
} Stats;

static bool phase(const mini_api_t *api, const char *name, uint32_t timeout)
{
    const mini_audio_tx_api_t *tx = api->audio->tx;
    mini_audio_stream_t stream = MINI_AUDIO_STREAM_INVALID;
    mini_audio_format_t format;
    format.struct_size = sizeof(format);
    format.sample_rate_hz = 48000;
    format.sample_format = MINI_AUDIO_SAMPLE_S16;
    format.channels = 1;
    static int16_t silence[BLOCK_FRAMES];
    Stats stats;
    /* Volatile byte stores avoid compiler-generated external memset imports. */
    volatile unsigned char *bytes = (volatile unsigned char *)&stats;
    for (unsigned i = 0; i < sizeof(stats); ++i) bytes[i] = 0;
    stats.min = UINT64_MAX;
    static const uint32_t thresholds[] = {1000, 2000, 5000, 10000, 20000};
    bool good = true;
    api->console->write(name);
    if (tx->open("speaker", &format, &stream) != MINI_OK) {
        api->console->write("open failed\n");
        return false;
    }
    if (tx->start(stream) != MINI_OK) {
        api->console->write("start failed\n");
        good = false;
        goto cleanup;
    }
    for (uint32_t i = 0; i < WARMUP_CALLS + MEASURE_CALLS; ++i) {
        uint32_t accepted = 0;
        uint64_t before = api->time_location->monotonic_us();
        mini_result_t result = tx->write(stream, silence, BLOCK_FRAMES, &accepted, timeout);
        uint64_t elapsed = api->time_location->monotonic_us() - before;
        if (i >= WARMUP_CALLS) {
            ++stats.calls;
            stats.frames += accepted;
            if (result == MINI_OK) ++stats.ok;
            else if (result == MINI_ERR_TIMEOUT) ++stats.timeout;
            else ++stats.other;
            if (accepted > 0 && accepted < BLOCK_FRAMES) ++stats.partial;
            if (result == MINI_OK && accepted == 0) ++stats.zero;
            stats.sum += elapsed;
            if (elapsed < stats.min) stats.min = elapsed;
            if (elapsed > stats.max) stats.max = elapsed;
            for (unsigned j = 0; j < 5; ++j) if (elapsed > thresholds[j]) ++stats.over[j];
        }
        if (accepted > BLOCK_FRAMES || (result != MINI_OK && result != MINI_ERR_TIMEOUT)) {
            api->console->write("write failure (possibly during warm-up); phase stopped\n");
            good = false;
            break;
        }
    }
cleanup:
    if (good && tx->stop(stream) != MINI_OK) good = false;
    if (!good) (void)tx->abort(stream);
    if (tx->close(stream) != MINI_OK) {
        api->console->write("close failed\n");
        good = false;
    }
    number(api, "calls=", stats.calls);
    number(api, "frames=", stats.frames);
    number(api, "OK=", stats.ok);
    number(api, "TIMEOUT=", stats.timeout);
    number(api, "other=", stats.other);
    number(api, "partial=", stats.partial);
    number(api, "zero_OK=", stats.zero);
    number(api, "min_us=", stats.calls ? stats.min : 0);
    uint32_t remainder;
    number(api, "mean_us=", stats.calls ? divide(stats.sum, stats.calls, &remainder) : 0);
    number(api, "max_us=", stats.max);
    static const char *const labels[] = {">1000us=", ">2000us=", ">5000us=", ">10000us=", ">20000us="};
    for (unsigned j = 0; j < 5; ++j) number(api, labels[j], stats.over[j]);
    return good;
}
int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    const mini_api_t *api = mini_api_get();
    if (!api || api->api_version != MINISHELL_API_VERSION || !api->console ||
        !api->console->write || !api->time_location || !api->time_location->monotonic_us ||
        !api->audio || !(api->audio->capabilities & MINI_AUDIO_CAP_TX) || !api->audio->tx)
        return 2;
    const mini_audio_tx_api_t *tx = api->audio->tx;
    if (!tx->open || !tx->start || !tx->write || !tx->stop || !tx->abort || !tx->close) return 2;
    api->console->write("audio_tx_probe: silent 48000 Hz S16 mono, 48 frames/call; warm-up=100, measured=5000 per phase\n");
    if (!phase(api, "phase A timeout_ms=20\n", 20)) return 1;
    if (!phase(api, "phase B timeout_ms=0 (MINI_WAIT_NONE)\n", MINI_WAIT_NONE)) return 1;
    api->console->write("audio_tx_probe: finished; capture both phase reports\n");
    return 0;
}

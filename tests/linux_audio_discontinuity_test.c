#include <assert.h>
#include <stdio.h>
#include "linux_audio_buffered.h"

/* Consumer controls completion of each fake provider read, including one
 * deliberately in flight while the consumer acknowledges the gap. */
static atomic_int requested, completed;
static mini_result_t next_result;
static int16_t next_sample;

static mini_result_t fake_open(void *ctx, const char *endpoint, uint32_t rate,
                               uint32_t format, uint32_t channels,
                               minishell_backend_audio_t *out)
{
    (void)ctx; (void)endpoint; (void)rate; (void)format; (void)channels;
    *out = 42;
    return MINI_OK;
}
static mini_result_t fake_lifecycle(void *ctx, minishell_backend_audio_t handle)
{
    (void)ctx; assert(handle == 42); return MINI_OK;
}
static mini_result_t fake_read(void *ctx, minishell_backend_audio_t handle, void *frames,
                               uint32_t capacity, uint32_t *got, uint32_t timeout)
{
    (void)ctx; (void)timeout; assert(handle == 42 && capacity > 0);
    int ticket = atomic_fetch_add(&requested, 1) + 1;
    while (atomic_load_explicit(&completed, memory_order_acquire) < ticket) {
        if (atomic_load(&s_linux_audio_buffered.stop_requested)) return MINI_ERR_NOT_READY;
        linux_audio_sleep_1ms();
    }
    ((int16_t *)frames)[0] = ((int16_t *)frames)[1] = next_sample;
    *got = 1;
    return next_result;
}
static void wait_for(atomic_int *value, int target)
{
    uint64_t deadline = linux_audio_monotonic_ms() + 3000;
    while (atomic_load(value) < target) {
        assert(linux_audio_monotonic_ms() < deadline);
        linux_audio_sleep_1ms();
    }
}
static void deliver(int ticket, mini_result_t result, int16_t sample)
{
    wait_for(&requested, ticket);
    next_result = result;
    next_sample = sample;
    atomic_store_explicit(&completed, ticket, memory_order_release);
    wait_for(&requested, ticket + 1);
}
int main(void)
{
    minishell_services_port_t port = {
        .audio_rx_open = fake_open, .audio_rx_start = fake_lifecycle,
        .audio_rx_read = fake_read, .audio_rx_stop = fake_lifecycle,
        .audio_rx_close = fake_lifecycle
    };
    linux_audio_buffered_configure(&port);
    minishell_backend_audio_t handle;
    assert(port.audio_rx_open(NULL, "alsa:fake", 12000, MINI_AUDIO_SAMPLE_S16, 2, &handle) == MINI_OK);
    assert(port.audio_rx_start(NULL, handle) == MINI_OK);
    deliver(1, MINI_OK, 11); /* old data stays queued */
    deliver(2, MINI_ERR_DISCONTINUITY, 22);
    deliver(3, MINI_OK, 33); /* busy consumer: drain and discard */
    deliver(4, MINI_ERR_DISCONTINUITY, 44); /* coalesce while pending */
    int16_t frames[8]; uint32_t got = 99;
    assert(port.audio_rx_read(NULL, handle, frames, 4, &got, MINI_WAIT_NONE) == MINI_ERR_DISCONTINUITY);
    assert(got == 0);
    assert(atomic_load(&requested) == 5); /* provider read already in flight at ACK */
    assert(atomic_load(&s_linux_audio_buffered.epoch) == LINUX_AUDIO_EPOCH_ACKNOWLEDGED);
    deliver(5, MINI_ERR_DISCONTINUITY, 55);
    assert(atomic_load(&s_linux_audio_buffered.epoch) == LINUX_AUDIO_EPOCH_PENDING);
    assert(port.audio_rx_read(NULL, handle, frames, 4, &got, MINI_WAIT_NONE) == MINI_ERR_DISCONTINUITY);
    assert(got == 0);
    deliver(6, MINI_OK, 66); /* read began before ACK: must still be discarded */
    assert(port.audio_rx_read(NULL, handle, frames, 4, &got, MINI_WAIT_NONE) == MINI_OK && got == 0);
    deliver(7, MINI_OK, 77);
    assert(port.audio_rx_read(NULL, handle, frames, 4, &got, MINI_WAIT_NONE) == MINI_OK);
    assert(got == 1 && frames[0] == 77 && frames[1] == 77);
    assert(atomic_load(&s_linux_audio_buffered.worker_result) == MINI_OK);
    next_result = MINI_ERR_IO;
    atomic_store_explicit(&completed, 8, memory_order_release);
    uint64_t deadline = linux_audio_monotonic_ms() + 3000;
    while (atomic_load(&s_linux_audio_buffered.worker_result) == MINI_OK) {
        assert(linux_audio_monotonic_ms() < deadline);
        linux_audio_sleep_1ms();
    }
    assert(port.audio_rx_read(NULL, handle, frames, 4, &got, MINI_WAIT_NONE) == MINI_ERR_IO && got == 0);
    assert(port.audio_rx_read(NULL, handle, frames, 4, &got, MINI_WAIT_NONE) == MINI_ERR_IO);
    assert(port.audio_rx_close(NULL, handle) == MINI_OK);
    puts("buffered discontinuity: PASS");
    return 0;
}

#pragma once

/*
 * Linux live-audio buffering wrapper.
 *
 * MiniFT8-V2 keeps USB/UAC reception running independently while decoding.
 * The Linux reference build is otherwise single-threaded: FT8 decoding runs
 * synchronously from the same loop that consumes Audio.read().  Without a
 * producer running in parallel, even a short decode can stop ALSA reads long
 * enough to overrun the capture stream and lose UTC/sample alignment.
 *
 * This wrapper preserves the generic MiniShell Audio contract.  Only `alsa:`
 * RX endpoints are buffered; file/WAV endpoints continue to delegate directly
 * to the underlying provider.  The ring stores the already-canonical
 * 12 kHz/S16/interleaved frames produced by linux_audio_wav.c, so no FT8 or
 * QMX semantics leak into this layer.
 *
 * It is header-local on purpose: linux_services.c is already in the Linux
 * build, so the wrapper can be added without another build-system source.
 */

#include <dlfcn.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "minishell_services.h"

#define LINUX_AUDIO_BUFFERED_HANDLE ((minishell_backend_audio_t)0xB001u)
#define LINUX_AUDIO_RING_FRAMES 65536u
#define LINUX_AUDIO_RING_MASK (LINUX_AUDIO_RING_FRAMES - 1u)
#define LINUX_AUDIO_WORKER_FRAMES 512u

_Static_assert((LINUX_AUDIO_RING_FRAMES & LINUX_AUDIO_RING_MASK) == 0u,
               "audio ring size must be a power of two");

typedef struct {
    void *ctx;
    mini_result_t (*open)(void *ctx, const char *endpoint,
                          uint32_t sample_rate_hz, uint32_t sample_format,
                          uint32_t channels, minishell_backend_audio_t *out_audio);
    mini_result_t (*start)(void *ctx, minishell_backend_audio_t audio);
    mini_result_t (*read)(void *ctx, minishell_backend_audio_t audio,
                          void *frames, uint32_t frame_capacity,
                          uint32_t *out_frames, uint32_t timeout_ms);
    mini_result_t (*stop)(void *ctx, minishell_backend_audio_t audio);
    mini_result_t (*close)(void *ctx, minishell_backend_audio_t audio);
} linux_audio_underlying_t;

/* Producer publishes PENDING and stops enqueueing. Consumer alone flushes then
 * publishes ACKNOWLEDGED. Producer acquires that ACK before starting a fresh read;
 * neither side resets the other side's monotonic ring counter. */
enum {
    LINUX_AUDIO_EPOCH_RUNNING,
    LINUX_AUDIO_EPOCH_PENDING,
    LINUX_AUDIO_EPOCH_ACKNOWLEDGED
};

typedef struct {
    linux_audio_underlying_t underlying;
    minishell_backend_audio_t underlying_handle;

    int16_t ring[LINUX_AUDIO_RING_FRAMES * 2u];
    atomic_uint_fast64_t write_count;
    atomic_uint_fast64_t read_count;
    atomic_int stop_requested;
    atomic_int worker_result;
    atomic_int epoch;

    void *pthread_library;
    int (*pthread_create_fn)(pthread_t *, const pthread_attr_t *,
                             void *(*)(void *), void *);
    int (*pthread_join_fn)(pthread_t, void **);
    pthread_t worker;
    int worker_started;
    int stream_started;
    int open;
} linux_audio_buffered_state_t;

static linux_audio_buffered_state_t s_linux_audio_buffered;

static int linux_audio_is_alsa_endpoint(const char *endpoint)
{
    return endpoint != NULL && strncmp(endpoint, "alsa:", 5u) == 0 && endpoint[5] != '\0';
}

static void linux_audio_sleep_1ms(void)
{
    struct timespec req = {.tv_sec = 0, .tv_nsec = 1000000L};
    (void)nanosleep(&req, NULL);
}

static uint64_t linux_audio_monotonic_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0u;
    return (uint64_t)ts.tv_sec * UINT64_C(1000) + (uint64_t)ts.tv_nsec / UINT64_C(1000000);
}

static int linux_audio_load_pthread(void)
{
    linux_audio_buffered_state_t *s = &s_linux_audio_buffered;
    if (s->pthread_create_fn != NULL && s->pthread_join_fn != NULL) return 1;

    void *lib = dlopen("libpthread.so.0", RTLD_NOW | RTLD_LOCAL);
    if (lib == NULL) lib = dlopen(NULL, RTLD_NOW | RTLD_LOCAL);
    if (lib == NULL) return 0;

    *(void **)(&s->pthread_create_fn) = dlsym(lib, "pthread_create");
    *(void **)(&s->pthread_join_fn) = dlsym(lib, "pthread_join");
    if (s->pthread_create_fn == NULL || s->pthread_join_fn == NULL) {
        dlclose(lib);
        s->pthread_create_fn = NULL;
        s->pthread_join_fn = NULL;
        return 0;
    }

    s->pthread_library = lib;
    return 1;
}

static void linux_audio_ring_reset(void)
{
    atomic_store_explicit(&s_linux_audio_buffered.write_count, 0u, memory_order_relaxed);
    atomic_store_explicit(&s_linux_audio_buffered.read_count, 0u, memory_order_relaxed);
    atomic_store_explicit(&s_linux_audio_buffered.stop_requested, 0, memory_order_relaxed);
    atomic_store_explicit(&s_linux_audio_buffered.worker_result, MINI_OK, memory_order_relaxed);
    atomic_store_explicit(&s_linux_audio_buffered.epoch, LINUX_AUDIO_EPOCH_RUNNING, memory_order_relaxed);
}

static void *linux_audio_capture_worker(void *arg)
{
    (void)arg;
    linux_audio_buffered_state_t *s = &s_linux_audio_buffered;
    int16_t temp[LINUX_AUDIO_WORKER_FRAMES * 2u];

    while (!atomic_load_explicit(&s->stop_requested, memory_order_acquire)) {
        int epoch = atomic_load_explicit(&s->epoch, memory_order_acquire);
        if (epoch == LINUX_AUDIO_EPOCH_ACKNOWLEDGED)
            atomic_store_explicit(&s->epoch, LINUX_AUDIO_EPOCH_RUNNING, memory_order_release);
        /* A read begun while pending is discarded even if ACK arrives mid-read. */
        bool discard = epoch == LINUX_AUDIO_EPOCH_PENDING;
        uint32_t got = 0u;
        mini_result_t result = s->underlying.read(
            s->underlying.ctx, s->underlying_handle,
            temp, LINUX_AUDIO_WORKER_FRAMES, &got, 100u);

        if (atomic_load_explicit(&s->stop_requested, memory_order_acquire)) break;

        if (result == MINI_ERR_DISCONTINUITY) {
            atomic_store_explicit(&s->epoch, LINUX_AUDIO_EPOCH_PENDING, memory_order_release);
            continue;
        }
        if (result == MINI_ERR_TIMEOUT || result == MINI_ERR_NOT_READY) continue;
        if (result != MINI_OK) {
            atomic_store_explicit(&s->worker_result, result, memory_order_release);
            break;
        }
        if (discard || got == 0u) continue;

        for (uint32_t i = 0u; i < got; ++i) {
            uint64_t write_count;
            uint64_t read_count;

            for (;;) {
                if (atomic_load_explicit(&s->stop_requested, memory_order_acquire))
                    return NULL;
                write_count = atomic_load_explicit(&s->write_count, memory_order_relaxed);
                read_count = atomic_load_explicit(&s->read_count, memory_order_acquire);
                if (write_count - read_count < LINUX_AUDIO_RING_FRAMES) break;
                linux_audio_sleep_1ms();
            }

            size_t slot = (size_t)(write_count & LINUX_AUDIO_RING_MASK);
            s->ring[slot * 2u + 0u] = temp[i * 2u + 0u];
            s->ring[slot * 2u + 1u] = temp[i * 2u + 1u];
            atomic_store_explicit(&s->write_count, write_count + 1u, memory_order_release);
        }
    }

    return NULL;
}

static mini_result_t linux_audio_buffered_open(void *ctx, const char *endpoint,
                                                uint32_t sample_rate_hz,
                                                uint32_t sample_format,
                                                uint32_t channels,
                                                minishell_backend_audio_t *out_audio)
{
    (void)ctx;
    linux_audio_buffered_state_t *s = &s_linux_audio_buffered;

    if (!linux_audio_is_alsa_endpoint(endpoint)) {
        return s->underlying.open(s->underlying.ctx, endpoint,
                                  sample_rate_hz, sample_format, channels,
                                  out_audio);
    }
    if (out_audio == NULL) return MINI_ERR_INVALID;
    *out_audio = MINISHELL_BACKEND_AUDIO_INVALID;
    if (s->open) return MINI_ERR_TOO_MANY_OPEN;

    minishell_backend_audio_t underlying = MINISHELL_BACKEND_AUDIO_INVALID;
    mini_result_t result = s->underlying.open(
        s->underlying.ctx, endpoint, sample_rate_hz, sample_format, channels,
        &underlying);
    if (result != MINI_OK) return result;

    s->underlying_handle = underlying;
    s->open = 1;
    s->stream_started = 0;
    s->worker_started = 0;
    linux_audio_ring_reset();
    *out_audio = LINUX_AUDIO_BUFFERED_HANDLE;
    return MINI_OK;
}

static mini_result_t linux_audio_buffered_start(void *ctx,
                                                 minishell_backend_audio_t audio)
{
    (void)ctx;
    linux_audio_buffered_state_t *s = &s_linux_audio_buffered;

    if (audio != LINUX_AUDIO_BUFFERED_HANDLE) {
        return s->underlying.start(s->underlying.ctx, audio);
    }
    if (!s->open || s->stream_started) return MINI_ERR_BAD_HANDLE;
    if (!linux_audio_load_pthread()) return MINI_ERR_UNSUPPORTED;

    linux_audio_ring_reset();
    mini_result_t result = s->underlying.start(s->underlying.ctx,
                                                s->underlying_handle);
    if (result != MINI_OK) return result;
    s->stream_started = 1;

    if (s->pthread_create_fn(&s->worker, NULL, linux_audio_capture_worker, NULL) != 0) {
        (void)s->underlying.stop(s->underlying.ctx, s->underlying_handle);
        s->stream_started = 0;
        return MINI_ERR_IO;
    }
    s->worker_started = 1;
    return MINI_OK;
}

static mini_result_t linux_audio_buffered_read(void *ctx,
                                                minishell_backend_audio_t audio,
                                                void *frames,
                                                uint32_t frame_capacity,
                                                uint32_t *out_frames,
                                                uint32_t timeout_ms)
{
    (void)ctx;
    linux_audio_buffered_state_t *s = &s_linux_audio_buffered;

    if (audio != LINUX_AUDIO_BUFFERED_HANDLE) {
        return s->underlying.read(s->underlying.ctx, audio, frames,
                                  frame_capacity, out_frames, timeout_ms);
    }
    if (out_frames == NULL) return MINI_ERR_INVALID;
    *out_frames = 0u;
    if (!s->open) return MINI_ERR_BAD_HANDLE;
    if (!s->stream_started) return MINI_ERR_NOT_READY;
    if (frame_capacity == 0u) return MINI_OK;
    if (frames == NULL) return MINI_ERR_INVALID;

    uint64_t deadline = 0u;
    if (timeout_ms != MINI_WAIT_FOREVER && timeout_ms != MINI_WAIT_NONE)
        deadline = linux_audio_monotonic_ms() + timeout_ms;

    for (;;) {
        if (atomic_load_explicit(&s->epoch, memory_order_acquire) == LINUX_AUDIO_EPOCH_PENDING) {
            uint64_t end = atomic_load_explicit(&s->write_count, memory_order_acquire);
            atomic_store_explicit(&s->read_count, end, memory_order_release);
            atomic_store_explicit(&s->epoch, LINUX_AUDIO_EPOCH_ACKNOWLEDGED, memory_order_release);
            return MINI_ERR_DISCONTINUITY;
        }
        uint64_t read_count = atomic_load_explicit(&s->read_count, memory_order_relaxed);
        uint64_t write_count = atomic_load_explicit(&s->write_count, memory_order_acquire);
        uint64_t available = write_count - read_count;

        if (available > 0u) {
            uint32_t take = frame_capacity;
            if ((uint64_t)take > available) take = (uint32_t)available;
            int16_t *dst = (int16_t *)frames;

            for (uint32_t i = 0u; i < take; ++i) {
                size_t slot = (size_t)((read_count + i) & LINUX_AUDIO_RING_MASK);
                dst[i * 2u + 0u] = s->ring[slot * 2u + 0u];
                dst[i * 2u + 1u] = s->ring[slot * 2u + 1u];
            }

            atomic_store_explicit(&s->read_count, read_count + take, memory_order_release);
            if (atomic_load_explicit(&s->epoch, memory_order_acquire) == LINUX_AUDIO_EPOCH_PENDING)
                continue;
            *out_frames = take;
            return MINI_OK;
        }

        mini_result_t worker_result =
            (mini_result_t)atomic_load_explicit(&s->worker_result, memory_order_acquire);
        if (worker_result != MINI_OK) return worker_result;
        if (timeout_ms == MINI_WAIT_NONE) return MINI_OK;
        if (timeout_ms != MINI_WAIT_FOREVER && linux_audio_monotonic_ms() >= deadline)
            return MINI_OK;
        linux_audio_sleep_1ms();
    }
}

static mini_result_t linux_audio_buffered_stop(void *ctx,
                                                minishell_backend_audio_t audio)
{
    (void)ctx;
    linux_audio_buffered_state_t *s = &s_linux_audio_buffered;

    if (audio != LINUX_AUDIO_BUFFERED_HANDLE) {
        return s->underlying.stop(s->underlying.ctx, audio);
    }
    if (!s->open) return MINI_ERR_BAD_HANDLE;

    atomic_store_explicit(&s->stop_requested, 1, memory_order_release);
    if (s->worker_started) {
        (void)s->pthread_join_fn(s->worker, NULL);
        s->worker_started = 0;
    }

    mini_result_t result = MINI_OK;
    if (s->stream_started) {
        result = s->underlying.stop(s->underlying.ctx, s->underlying_handle);
        s->stream_started = 0;
    }
    return result;
}

static mini_result_t linux_audio_buffered_close(void *ctx,
                                                 minishell_backend_audio_t audio)
{
    (void)ctx;
    linux_audio_buffered_state_t *s = &s_linux_audio_buffered;

    if (audio != LINUX_AUDIO_BUFFERED_HANDLE) {
        return s->underlying.close(s->underlying.ctx, audio);
    }
    if (!s->open) return MINI_ERR_BAD_HANDLE;

    if (s->stream_started || s->worker_started) {
        mini_result_t stop_result = linux_audio_buffered_stop(NULL, audio);
        if (stop_result != MINI_OK) return stop_result;
    }

    mini_result_t result = s->underlying.close(s->underlying.ctx,
                                                s->underlying_handle);
    if (result == MINI_OK) {
        s->underlying_handle = MINISHELL_BACKEND_AUDIO_INVALID;
        s->open = 0;
        linux_audio_ring_reset();
    }
    return result;
}

static void linux_audio_buffered_configure(minishell_services_port_t *port)
{
    linux_audio_buffered_state_t *s = &s_linux_audio_buffered;
    if (port == NULL || port->audio_rx_open == NULL || port->audio_rx_start == NULL ||
        port->audio_rx_read == NULL || port->audio_rx_stop == NULL ||
        port->audio_rx_close == NULL) {
        return;
    }

    memset(s, 0, sizeof(*s));
    s->underlying.ctx = port->ctx;
    s->underlying.open = port->audio_rx_open;
    s->underlying.start = port->audio_rx_start;
    s->underlying.read = port->audio_rx_read;
    s->underlying.stop = port->audio_rx_stop;
    s->underlying.close = port->audio_rx_close;
    linux_audio_ring_reset();

    port->audio_rx_open = linux_audio_buffered_open;
    port->audio_rx_start = linux_audio_buffered_start;
    port->audio_rx_read = linux_audio_buffered_read;
    port->audio_rx_stop = linux_audio_buffered_stop;
    port->audio_rx_close = linux_audio_buffered_close;
}

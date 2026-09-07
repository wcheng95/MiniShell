#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "linux_audio_wav.h"

#define WAV_AUDIO_HANDLE ((minishell_backend_audio_t)1u)
#define WAV_PATH_MAX 4096u

typedef struct {
    char root_dir[WAV_PATH_MAX];
    FILE *file;
    uint32_t frame_bytes;
    uint64_t remaining_bytes;
    bool started;
} wav_state_t;

static wav_state_t s_wav;

static mini_result_t result_from_errno(int error)
{
    switch (error) {
        case 0: return MINI_OK;
        case EINVAL: return MINI_ERR_INVALID;
        case ENOENT: return MINI_ERR_NOT_FOUND;
        case EACCES:
        case EPERM:
        case EROFS: return MINI_ERR_ACCESS;
        case ENAMETOOLONG: return MINI_ERR_NAME_TOO_LONG;
        case ENOMEM: return MINI_ERR_NO_MEMORY;
        default: return MINI_ERR_IO;
    }
}

static uint16_t read_u16_le(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t read_u32_le(const uint8_t *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static bool read_exact(FILE *file, void *buffer, size_t size)
{
    return size == 0u || fread(buffer, 1u, size, file) == size;
}

static bool skip_bytes(FILE *file, uint64_t count)
{
    while (count > 0u) {
        long step = count > (uint64_t)LONG_MAX ? LONG_MAX : (long)count;
        if (fseek(file, step, SEEK_CUR) != 0) return false;
        count -= (uint64_t)step;
    }
    return true;
}

static mini_result_t logical_to_native(const char *logical, char *out, size_t out_size)
{
    if (logical == NULL || logical[0] != '/' || out == NULL || out_size == 0u ||
        s_wav.root_dir[0] == '\0') {
        return MINI_ERR_INVALID;
    }

    int written;
    if (strcmp(logical, "/") == 0) {
        written = snprintf(out, out_size, "%s", s_wav.root_dir);
    } else {
        written = snprintf(out, out_size, "%s%s", s_wav.root_dir, logical);
    }
    if (written < 0 || (size_t)written >= out_size) return MINI_ERR_NAME_TOO_LONG;
    return MINI_OK;
}

static mini_result_t parse_wav(FILE *file,
                               uint32_t requested_rate,
                               uint32_t requested_format,
                               uint32_t requested_channels,
                               uint32_t *out_frame_bytes,
                               uint64_t *out_data_bytes)
{
    uint8_t riff[12];
    if (!read_exact(file, riff, sizeof(riff))) return MINI_ERR_IO;
    if (memcmp(riff, "RIFF", 4u) != 0 || memcmp(riff + 8u, "WAVE", 4u) != 0) {
        return MINI_ERR_UNSUPPORTED;
    }

    bool have_fmt = false;
    bool have_data = false;
    uint16_t audio_format = 0u;
    uint16_t channels = 0u;
    uint16_t bits_per_sample = 0u;
    uint16_t block_align = 0u;
    uint32_t sample_rate = 0u;
    long data_offset = 0;
    uint32_t data_size = 0u;

    for (;;) {
        uint8_t chunk[8];
        if (!read_exact(file, chunk, sizeof(chunk))) break;
        uint32_t size = read_u32_le(chunk + 4u);

        if (memcmp(chunk, "fmt ", 4u) == 0) {
            if (size < 16u) return MINI_ERR_UNSUPPORTED;
            uint8_t fmt[16];
            if (!read_exact(file, fmt, sizeof(fmt))) return MINI_ERR_IO;
            audio_format = read_u16_le(fmt + 0u);
            channels = read_u16_le(fmt + 2u);
            sample_rate = read_u32_le(fmt + 4u);
            block_align = read_u16_le(fmt + 12u);
            bits_per_sample = read_u16_le(fmt + 14u);
            if (!skip_bytes(file, (uint64_t)size - sizeof(fmt))) return MINI_ERR_IO;
            have_fmt = true;
        } else if (memcmp(chunk, "data", 4u) == 0) {
            long offset = ftell(file);
            if (offset < 0) return MINI_ERR_IO;
            data_offset = offset;
            data_size = size;
            have_data = true;
            if (!skip_bytes(file, size)) return MINI_ERR_IO;
        } else {
            if (!skip_bytes(file, size)) return MINI_ERR_IO;
        }

        if ((size & 1u) != 0u && !skip_bytes(file, 1u)) return MINI_ERR_IO;
        if (have_fmt && have_data) break;
    }

    if (!have_fmt || !have_data) return MINI_ERR_UNSUPPORTED;
    if (audio_format != 1u || bits_per_sample != 16u) return MINI_ERR_UNSUPPORTED;
    if (requested_format != MINI_AUDIO_SAMPLE_S16 ||
        sample_rate != requested_rate || channels != requested_channels) {
        return MINI_ERR_UNSUPPORTED;
    }

    uint32_t expected_align = (uint32_t)channels * 2u;
    if (channels == 0u || block_align != expected_align || block_align == 0u ||
        (data_size % block_align) != 0u) {
        return MINI_ERR_UNSUPPORTED;
    }

    if (fseek(file, data_offset, SEEK_SET) != 0) return MINI_ERR_IO;
    *out_frame_bytes = block_align;
    *out_data_bytes = data_size;
    return MINI_OK;
}

static mini_result_t wav_rx_open(void *ctx, const char *endpoint,
                                 uint32_t sample_rate_hz, uint32_t sample_format,
                                 uint32_t channels,
                                 minishell_backend_audio_t *out_audio)
{
    (void)ctx;
    if (out_audio == NULL) return MINI_ERR_INVALID;
    *out_audio = MINISHELL_BACKEND_AUDIO_INVALID;
    if (endpoint == NULL) return MINI_ERR_NOT_FOUND;
    if (s_wav.file != NULL) return MINI_ERR_TOO_MANY_OPEN;

    char native[WAV_PATH_MAX];
    mini_result_t result = logical_to_native(endpoint, native, sizeof(native));
    if (result != MINI_OK) return result;

    FILE *file = fopen(native, "rb");
    if (file == NULL) return result_from_errno(errno);

    uint32_t frame_bytes = 0u;
    uint64_t data_bytes = 0u;
    result = parse_wav(file, sample_rate_hz, sample_format, channels,
                       &frame_bytes, &data_bytes);
    if (result != MINI_OK) {
        (void)fclose(file);
        return result;
    }

    s_wav.file = file;
    s_wav.frame_bytes = frame_bytes;
    s_wav.remaining_bytes = data_bytes;
    s_wav.started = false;
    *out_audio = WAV_AUDIO_HANDLE;
    return MINI_OK;
}

static bool valid_handle(minishell_backend_audio_t audio)
{
    return audio == WAV_AUDIO_HANDLE && s_wav.file != NULL;
}

static mini_result_t wav_rx_start(void *ctx, minishell_backend_audio_t audio)
{
    (void)ctx;
    if (!valid_handle(audio)) return MINI_ERR_BAD_HANDLE;
    s_wav.started = true;
    return MINI_OK;
}

static mini_result_t wav_rx_read(void *ctx, minishell_backend_audio_t audio,
                                 void *frames, uint32_t frame_capacity,
                                 uint32_t *out_frames, uint32_t timeout_ms)
{
    (void)ctx;
    (void)timeout_ms;
    if (!valid_handle(audio)) return MINI_ERR_BAD_HANDLE;
    if (out_frames == NULL) return MINI_ERR_INVALID;
    *out_frames = 0u;
    if (!s_wav.started) return MINI_ERR_NOT_READY;
    if (frame_capacity == 0u) return MINI_OK;
    if (frames == NULL) return MINI_ERR_INVALID;
    if (s_wav.remaining_bytes == 0u) return MINI_ERR_END_OF_STREAM;

    uint64_t remaining_frames = s_wav.remaining_bytes / s_wav.frame_bytes;
    uint32_t requested = frame_capacity;
    if ((uint64_t)requested > remaining_frames) requested = (uint32_t)remaining_frames;

    size_t got = fread(frames, s_wav.frame_bytes, requested, s_wav.file);
    if (got == 0u && requested != 0u) return MINI_ERR_IO;

    uint64_t bytes = (uint64_t)got * s_wav.frame_bytes;
    s_wav.remaining_bytes -= bytes;
    *out_frames = (uint32_t)got;

    if (got != requested) return MINI_ERR_IO;
    return MINI_OK;
}

static mini_result_t wav_rx_stop(void *ctx, minishell_backend_audio_t audio)
{
    (void)ctx;
    if (!valid_handle(audio)) return MINI_ERR_BAD_HANDLE;
    s_wav.started = false;
    return MINI_OK;
}

static mini_result_t wav_rx_close(void *ctx, minishell_backend_audio_t audio)
{
    (void)ctx;
    if (!valid_handle(audio)) return MINI_ERR_BAD_HANDLE;
    int result = fclose(s_wav.file);
    s_wav.file = NULL;
    s_wav.frame_bytes = 0u;
    s_wav.remaining_bytes = 0u;
    s_wav.started = false;
    return result == 0 ? MINI_OK : result_from_errno(errno);
}

void linux_audio_wav_shutdown(void)
{
    if (s_wav.file != NULL) {
        (void)fclose(s_wav.file);
        s_wav.file = NULL;
    }
    s_wav.frame_bytes = 0u;
    s_wav.remaining_bytes = 0u;
    s_wav.started = false;
}

void linux_audio_wav_configure(minishell_services_port_t *port, const char *root_dir)
{
    linux_audio_wav_shutdown();
    s_wav.root_dir[0] = '\0';
    if (port == NULL || root_dir == NULL || root_dir[0] == '\0') return;

    int written = snprintf(s_wav.root_dir, sizeof(s_wav.root_dir), "%s", root_dir);
    if (written < 0 || (size_t)written >= sizeof(s_wav.root_dir)) {
        s_wav.root_dir[0] = '\0';
        return;
    }

    port->audio_capabilities |= MINI_AUDIO_CAP_RX;
    port->audio_rx_open = wav_rx_open;
    port->audio_rx_start = wav_rx_start;
    port->audio_rx_read = wav_rx_read;
    port->audio_rx_stop = wav_rx_stop;
    port->audio_rx_close = wav_rx_close;
}

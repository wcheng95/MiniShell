#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "adv_internal.h"

#define WAV_AUDIO_HANDLE ((minishell_backend_audio_t)1u)

typedef struct {
    void *ctx;
    mini_result_t (*fs_open)(void *ctx, const char *path, uint32_t flags,
                             minishell_backend_file_t *out_file);
    mini_result_t (*fs_close)(void *ctx, minishell_backend_file_t file);
    mini_result_t (*fs_read)(void *ctx, minishell_backend_file_t file,
                             void *buffer, uint32_t size, uint32_t *out_read);
    mini_result_t (*fs_seek)(void *ctx, minishell_backend_file_t file,
                             int64_t offset, uint32_t origin, uint64_t *out_position);
    minishell_backend_file_t file;
    uint32_t frame_bytes;
    uint64_t remaining_bytes;
    bool started;
} wav_state_t;

static wav_state_t s_wav;

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

static mini_result_t read_exact(minishell_backend_file_t file, void *buffer,
                                uint32_t size)
{
    uint8_t *dst = buffer;
    uint32_t total = 0u;

    while (total < size) {
        uint32_t got = 0u;
        mini_result_t result = s_wav.fs_read(s_wav.ctx, file, dst + total,
                                             size - total, &got);
        if (result != MINI_OK) return result;
        if (got == 0u) return MINI_ERR_IO;
        total += got;
    }
    return MINI_OK;
}

static mini_result_t skip_bytes(minishell_backend_file_t file, uint64_t count)
{
    while (count > 0u) {
        int64_t step = count > (uint64_t)INT64_MAX ? INT64_MAX : (int64_t)count;
        uint64_t position = 0u;
        mini_result_t result = s_wav.fs_seek(s_wav.ctx, file, step,
                                             MINI_FS_SEEK_CUR, &position);
        if (result != MINI_OK) return result;
        count -= (uint64_t)step;
    }
    return MINI_OK;
}

static mini_result_t current_position(minishell_backend_file_t file,
                                      uint64_t *out_position)
{
    return s_wav.fs_seek(s_wav.ctx, file, 0, MINI_FS_SEEK_CUR, out_position);
}

static mini_result_t parse_wav(minishell_backend_file_t file,
                               uint32_t requested_rate,
                               uint32_t requested_format,
                               uint32_t requested_channels,
                               uint32_t *out_frame_bytes,
                               uint64_t *out_data_bytes)
{
    uint8_t riff[12];
    mini_result_t result = read_exact(file, riff, sizeof(riff));
    if (result != MINI_OK) return result;
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
    uint64_t data_offset = 0u;
    uint32_t data_size = 0u;

    for (;;) {
        uint8_t chunk[8];
        uint32_t got = 0u;
        result = s_wav.fs_read(s_wav.ctx, file, chunk, sizeof(chunk), &got);
        if (result != MINI_OK) return result;
        if (got == 0u) break;
        if (got != sizeof(chunk)) return MINI_ERR_IO;

        uint32_t size = read_u32_le(chunk + 4u);
        if (memcmp(chunk, "fmt ", 4u) == 0) {
            if (size < 16u) return MINI_ERR_UNSUPPORTED;
            uint8_t fmt[16];
            result = read_exact(file, fmt, sizeof(fmt));
            if (result != MINI_OK) return result;
            audio_format = read_u16_le(fmt + 0u);
            channels = read_u16_le(fmt + 2u);
            sample_rate = read_u32_le(fmt + 4u);
            block_align = read_u16_le(fmt + 12u);
            bits_per_sample = read_u16_le(fmt + 14u);
            result = skip_bytes(file, (uint64_t)size - sizeof(fmt));
            if (result != MINI_OK) return result;
            have_fmt = true;
        } else if (memcmp(chunk, "data", 4u) == 0) {
            result = current_position(file, &data_offset);
            if (result != MINI_OK) return result;
            data_size = size;
            have_data = true;
            result = skip_bytes(file, size);
            if (result != MINI_OK) return result;
        } else {
            result = skip_bytes(file, size);
            if (result != MINI_OK) return result;
        }

        if ((size & 1u) != 0u) {
            result = skip_bytes(file, 1u);
            if (result != MINI_OK) return result;
        }
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

    uint64_t ignored = 0u;
    result = s_wav.fs_seek(s_wav.ctx, file, (int64_t)data_offset,
                           MINI_FS_SEEK_SET, &ignored);
    if (result != MINI_OK) return result;

    *out_frame_bytes = block_align;
    *out_data_bytes = data_size;
    return MINI_OK;
}

static bool valid_handle(minishell_backend_audio_t audio)
{
    return audio == WAV_AUDIO_HANDLE && s_wav.file != MINISHELL_BACKEND_FILE_INVALID;
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
    if (s_wav.file != MINISHELL_BACKEND_FILE_INVALID) return MINI_ERR_TOO_MANY_OPEN;

    minishell_backend_file_t file = MINISHELL_BACKEND_FILE_INVALID;
    mini_result_t result = s_wav.fs_open(s_wav.ctx, endpoint, MINI_FS_READ, &file);
    if (result != MINI_OK) return result;

    uint32_t frame_bytes = 0u;
    uint64_t data_bytes = 0u;
    result = parse_wav(file, sample_rate_hz, sample_format, channels,
                       &frame_bytes, &data_bytes);
    if (result != MINI_OK) {
        (void)s_wav.fs_close(s_wav.ctx, file);
        return result;
    }

    s_wav.file = file;
    s_wav.frame_bytes = frame_bytes;
    s_wav.remaining_bytes = data_bytes;
    s_wav.started = false;
    *out_audio = WAV_AUDIO_HANDLE;
    return MINI_OK;
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
    uint32_t requested_frames = frame_capacity;
    if ((uint64_t)requested_frames > remaining_frames) {
        requested_frames = (uint32_t)remaining_frames;
    }

    uint64_t requested_bytes64 = (uint64_t)requested_frames * s_wav.frame_bytes;
    if (requested_bytes64 > UINT32_MAX) return MINI_ERR_INVALID;
    uint32_t requested_bytes = (uint32_t)requested_bytes64;

    uint8_t *dst = frames;
    uint32_t total = 0u;
    while (total < requested_bytes) {
        uint32_t got = 0u;
        mini_result_t result = s_wav.fs_read(s_wav.ctx, s_wav.file, dst + total,
                                             requested_bytes - total, &got);
        if (result != MINI_OK) return result;
        if (got == 0u) return MINI_ERR_IO;
        total += got;
    }

    s_wav.remaining_bytes -= total;
    *out_frames = requested_frames;
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
    mini_result_t result = s_wav.fs_close(s_wav.ctx, s_wav.file);
    if (result == MINI_OK) {
        s_wav.file = MINISHELL_BACKEND_FILE_INVALID;
        s_wav.frame_bytes = 0u;
        s_wav.remaining_bytes = 0u;
        s_wav.started = false;
    }
    return result;
}

void adv_audio_wav_configure(minishell_services_port_t *port)
{
    memset(&s_wav, 0, sizeof(s_wav));
    s_wav.file = MINISHELL_BACKEND_FILE_INVALID;
    if (port == NULL || port->fs_open == NULL || port->fs_close == NULL ||
        port->fs_read == NULL || port->fs_seek == NULL) {
        return;
    }

    s_wav.ctx = port->ctx;
    s_wav.fs_open = port->fs_open;
    s_wav.fs_close = port->fs_close;
    s_wav.fs_read = port->fs_read;
    s_wav.fs_seek = port->fs_seek;

    port->audio_capabilities |= MINI_AUDIO_CAP_RX;
    port->audio_rx_open = wav_rx_open;
    port->audio_rx_start = wav_rx_start;
    port->audio_rx_read = wav_rx_read;
    port->audio_rx_stop = wav_rx_stop;
    port->audio_rx_close = wav_rx_close;
}

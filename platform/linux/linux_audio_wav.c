#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#if defined(__has_include)
#  if __has_include(<alsa/asoundlib.h>)
#    define MINISHELL_LINUX_HAVE_ALSA 1
#  endif
#endif

#ifdef MINISHELL_LINUX_HAVE_ALSA
#include <dlfcn.h>
#include <errno.h>
#include <limits.h>
#include <alsa/asoundlib.h>
#endif

#include "linux_audio_wav.h"

#define WAV_AUDIO_HANDLE ((minishell_backend_audio_t)1u)
#define ALSA_AUDIO_HANDLE ((minishell_backend_audio_t)2u)

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

#ifdef MINISHELL_LINUX_HAVE_ALSA
typedef struct {
    void *library;
    snd_pcm_t *pcm;
    bool started;
    uint8_t decimation_phase;

    int (*pcm_open)(snd_pcm_t **pcm, const char *name,
                    snd_pcm_stream_t stream, int mode);
    int (*pcm_close)(snd_pcm_t *pcm);
    int (*pcm_set_params)(snd_pcm_t *pcm,
                          snd_pcm_format_t format,
                          snd_pcm_access_t access,
                          unsigned int channels,
                          unsigned int rate,
                          int soft_resample,
                          unsigned int latency);
    int (*pcm_prepare)(snd_pcm_t *pcm);
    int (*pcm_start)(snd_pcm_t *pcm);
    int (*pcm_wait)(snd_pcm_t *pcm, int timeout);
    snd_pcm_sframes_t (*pcm_readi)(snd_pcm_t *pcm, void *buffer,
                                   snd_pcm_uframes_t size);
    int (*pcm_recover)(snd_pcm_t *pcm, int err, int silent);
    int (*pcm_drop)(snd_pcm_t *pcm);
} alsa_state_t;

static alsa_state_t s_alsa;

#define ALSA_NATIVE_RATE 48000u
#define ALSA_NATIVE_CHANNELS 2u
#define ALSA_DECIMATION 4u
#define ALSA_NATIVE_FRAME_BYTES 6u
#define ALSA_NATIVE_CHUNK_FRAMES 256u
#define ALSA_TARGET_LATENCY_US 10000u
#endif

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

static bool valid_wav_handle(minishell_backend_audio_t audio)
{
    return audio == WAV_AUDIO_HANDLE && s_wav.file != MINISHELL_BACKEND_FILE_INVALID;
}

#ifdef MINISHELL_LINUX_HAVE_ALSA
static bool valid_alsa_handle(minishell_backend_audio_t audio)
{
    return audio == ALSA_AUDIO_HANDLE && s_alsa.pcm != NULL;
}

static bool is_alsa_endpoint(const char *endpoint)
{
    return endpoint != NULL && strncmp(endpoint, "alsa:", 5u) == 0 && endpoint[5] != '\0';
}

static bool alsa_load(void)
{
    if (s_alsa.library != NULL) return true;

    void *library = dlopen("libasound.so.2", RTLD_NOW | RTLD_LOCAL);
    if (library == NULL) return false;

#define LOAD_ALSA(name)                                                        \
    do {                                                                       \
        *(void **)(&s_alsa.name) = dlsym(library, "snd_" #name);              \
        if (s_alsa.name == NULL) {                                             \
            dlclose(library);                                                  \
            memset(&s_alsa, 0, sizeof(s_alsa));                               \
            return false;                                                      \
        }                                                                      \
    } while (0)

    LOAD_ALSA(pcm_open);
    LOAD_ALSA(pcm_close);
    LOAD_ALSA(pcm_set_params);
    LOAD_ALSA(pcm_prepare);
    LOAD_ALSA(pcm_start);
    LOAD_ALSA(pcm_wait);
    LOAD_ALSA(pcm_readi);
    LOAD_ALSA(pcm_recover);
    LOAD_ALSA(pcm_drop);
#undef LOAD_ALSA

    s_alsa.library = library;
    return true;
}

static int16_t s24_to_s16(const uint8_t *p)
{
    int32_t value = (int32_t)p[0] |
                    ((int32_t)p[1] << 8) |
                    ((int32_t)p[2] << 16);
    if ((value & 0x00800000) != 0) value |= (int32_t)0xff000000;
    return (int16_t)(value >> 8);
}

static mini_result_t alsa_rx_open(const char *endpoint,
                                  uint32_t sample_rate_hz,
                                  uint32_t sample_format,
                                  uint32_t channels,
                                  minishell_backend_audio_t *out_audio)
{
    if (out_audio == NULL) return MINI_ERR_INVALID;
    *out_audio = MINISHELL_BACKEND_AUDIO_INVALID;
    if (!is_alsa_endpoint(endpoint)) return MINI_ERR_NOT_FOUND;
    if (s_alsa.pcm != NULL) return MINI_ERR_TOO_MANY_OPEN;
    if (sample_rate_hz != 12000u || sample_format != MINI_AUDIO_SAMPLE_S16 || channels != 2u) {
        return MINI_ERR_UNSUPPORTED;
    }
    if (!alsa_load()) return MINI_ERR_UNSUPPORTED;

    snd_pcm_t *pcm = NULL;
    if (s_alsa.pcm_open(&pcm, endpoint + 5u, SND_PCM_STREAM_CAPTURE, 0) < 0) {
        return MINI_ERR_IO;
    }

    if (s_alsa.pcm_set_params(pcm,
                              SND_PCM_FORMAT_S24_3LE,
                              SND_PCM_ACCESS_RW_INTERLEAVED,
                              ALSA_NATIVE_CHANNELS,
                              ALSA_NATIVE_RATE,
                              0,
                              ALSA_TARGET_LATENCY_US) < 0) {
        (void)s_alsa.pcm_close(pcm);
        return MINI_ERR_UNSUPPORTED;
    }

    s_alsa.pcm = pcm;
    s_alsa.started = false;
    s_alsa.decimation_phase = 0u;
    *out_audio = ALSA_AUDIO_HANDLE;
    return MINI_OK;
}
#endif

static mini_result_t audio_rx_open(void *ctx, const char *endpoint,
                                   uint32_t sample_rate_hz, uint32_t sample_format,
                                   uint32_t channels,
                                   minishell_backend_audio_t *out_audio)
{
    (void)ctx;
#ifdef MINISHELL_LINUX_HAVE_ALSA
    if (is_alsa_endpoint(endpoint)) {
        return alsa_rx_open(endpoint, sample_rate_hz, sample_format, channels, out_audio);
    }
#endif

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

static mini_result_t audio_rx_start(void *ctx, minishell_backend_audio_t audio)
{
    (void)ctx;
#ifdef MINISHELL_LINUX_HAVE_ALSA
    if (valid_alsa_handle(audio)) {
        if (s_alsa.pcm_prepare(s_alsa.pcm) < 0) return MINI_ERR_IO;
        if (s_alsa.pcm_start(s_alsa.pcm) < 0) return MINI_ERR_IO;
        s_alsa.started = true;
        s_alsa.decimation_phase = 0u;
        return MINI_OK;
    }
#endif
    if (!valid_wav_handle(audio)) return MINI_ERR_BAD_HANDLE;
    s_wav.started = true;
    return MINI_OK;
}

static mini_result_t audio_rx_read(void *ctx, minishell_backend_audio_t audio,
                                   void *frames, uint32_t frame_capacity,
                                   uint32_t *out_frames, uint32_t timeout_ms)
{
    (void)ctx;
    if (out_frames == NULL) return MINI_ERR_INVALID;
    *out_frames = 0u;

#ifdef MINISHELL_LINUX_HAVE_ALSA
    if (valid_alsa_handle(audio)) {
        uint8_t native[ALSA_NATIVE_CHUNK_FRAMES * ALSA_NATIVE_FRAME_BYTES];
        int16_t *dst = (int16_t *)frames;
        uint32_t produced = 0u;

        if (!s_alsa.started) return MINI_ERR_NOT_READY;
        if (frame_capacity == 0u) return MINI_OK;
        if (frames == NULL) return MINI_ERR_INVALID;

        int wait_ms = timeout_ms > (uint32_t)INT_MAX ? INT_MAX : (int)timeout_ms;
        int ready = s_alsa.pcm_wait(s_alsa.pcm, wait_ms);
        if (ready == 0) return MINI_OK;
        if (ready < 0) {
            if (s_alsa.pcm_recover(s_alsa.pcm, ready, 1) < 0) return MINI_ERR_IO;
            s_alsa.decimation_phase = 0u;
            return MINI_ERR_DISCONTINUITY;
        }

        while (produced < frame_capacity) {
            uint32_t remaining = frame_capacity - produced;
            snd_pcm_uframes_t want = (snd_pcm_uframes_t)remaining * ALSA_DECIMATION;
            if (want > ALSA_NATIVE_CHUNK_FRAMES) want = ALSA_NATIVE_CHUNK_FRAMES;

            snd_pcm_sframes_t got = s_alsa.pcm_readi(s_alsa.pcm, native, want);
            if (got == -EAGAIN) break;
            if (got < 0) {
                if (s_alsa.pcm_recover(s_alsa.pcm, (int)got, 1) < 0) return MINI_ERR_IO;
                s_alsa.decimation_phase = 0u;
                return MINI_ERR_DISCONTINUITY;
            }
            if (got == 0) break;

            for (snd_pcm_sframes_t i = 0; i < got; ++i) {
                const uint8_t *src = native + (size_t)i * ALSA_NATIVE_FRAME_BYTES;
                if (s_alsa.decimation_phase == 0u) {
                    if (produced >= frame_capacity) break;
                    dst[produced * 2u + 0u] = s24_to_s16(src + 0u);
                    dst[produced * 2u + 1u] = s24_to_s16(src + 3u);
                    ++produced;
                }
                s_alsa.decimation_phase =
                    (uint8_t)((s_alsa.decimation_phase + 1u) % ALSA_DECIMATION);
            }

            if ((snd_pcm_uframes_t)got < want) break;
        }

        *out_frames = produced;
        return MINI_OK;
    }
#endif

    if (!valid_wav_handle(audio)) return MINI_ERR_BAD_HANDLE;
    (void)timeout_ms;
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

static mini_result_t audio_rx_stop(void *ctx, minishell_backend_audio_t audio)
{
    (void)ctx;
#ifdef MINISHELL_LINUX_HAVE_ALSA
    if (valid_alsa_handle(audio)) {
        (void)s_alsa.pcm_drop(s_alsa.pcm);
        s_alsa.started = false;
        return MINI_OK;
    }
#endif
    if (!valid_wav_handle(audio)) return MINI_ERR_BAD_HANDLE;
    s_wav.started = false;
    return MINI_OK;
}

static mini_result_t audio_rx_close(void *ctx, minishell_backend_audio_t audio)
{
    (void)ctx;
#ifdef MINISHELL_LINUX_HAVE_ALSA
    if (valid_alsa_handle(audio)) {
        (void)s_alsa.pcm_close(s_alsa.pcm);
        s_alsa.pcm = NULL;
        s_alsa.started = false;
        s_alsa.decimation_phase = 0u;
        return MINI_OK;
    }
#endif
    if (!valid_wav_handle(audio)) return MINI_ERR_BAD_HANDLE;
    mini_result_t result = s_wav.fs_close(s_wav.ctx, s_wav.file);
    if (result == MINI_OK) {
        s_wav.file = MINISHELL_BACKEND_FILE_INVALID;
        s_wav.frame_bytes = 0u;
        s_wav.remaining_bytes = 0u;
        s_wav.started = false;
    }
    return result;
}

void linux_audio_wav_configure(minishell_services_port_t *port)
{
    memset(&s_wav, 0, sizeof(s_wav));
    s_wav.file = MINISHELL_BACKEND_FILE_INVALID;
#ifdef MINISHELL_LINUX_HAVE_ALSA
    memset(&s_alsa, 0, sizeof(s_alsa));
#endif
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
    port->audio_rx_open = audio_rx_open;
    port->audio_rx_start = audio_rx_start;
    port->audio_rx_read = audio_rx_read;
    port->audio_rx_stop = audio_rx_stop;
    port->audio_rx_close = audio_rx_close;
}

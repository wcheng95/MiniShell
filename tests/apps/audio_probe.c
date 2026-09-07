#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "minishell/api.h"

static int fail(const mini_api_t *api, const char *message, int code)
{
    if (api != NULL && api->system != NULL && api->system->write != NULL) {
        api->system->write("audio_probe: FAIL: ");
        api->system->write(message);
        api->system->write("\n");
    }
    return code;
}

static uint64_t fnv1a_bytes(uint64_t hash, const uint8_t *data, uint32_t size)
{
    for (uint32_t i = 0u; i < size; ++i) {
        hash ^= data[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

int main(int argc, char **argv)
{
    const mini_api_t *api = mini_api_get();
    if (api == NULL || api->abi_version != MINISHELL_ABI_VERSION ||
        api->system == NULL || api->system->write == NULL) {
        return 2;
    }

    const uint32_t audio_field_end =
        (uint32_t)(offsetof(mini_api_t, audio) + sizeof(api->audio));
    if (api->struct_size < audio_field_end || api->audio == NULL ||
        (api->audio->capabilities & MINI_AUDIO_CAP_RX) == 0u ||
        api->audio->rx == NULL) {
        return fail(api, "RX Audio unavailable", 3);
    }
    if (argc != 2) return fail(api, "usage: audio_probe <logical-wav-path>", 4);

    mini_audio_format_t format = {
        .struct_size = sizeof(format),
        .sample_rate_hz = 12000u,
        .sample_format = MINI_AUDIO_SAMPLE_S16,
        .channels = 2u,
    };

    mini_audio_stream_t stream = MINI_AUDIO_STREAM_INVALID;
    mini_result_t result = api->audio->rx->open(argv[1], &format, &stream);
    if (result != MINI_OK) return fail(api, "open", 5);

    result = api->audio->rx->start(stream);
    if (result != MINI_OK) {
        (void)api->audio->rx->close(stream);
        return fail(api, "start", 6);
    }

    int16_t frames[512u * 2u];
    uint64_t frame_count = 0u;
    uint64_t hash = UINT64_C(14695981039346656037);

    for (;;) {
        uint32_t got = 0u;
        result = api->audio->rx->read(stream, frames, 512u, &got, MINI_WAIT_NONE);
        if (result == MINI_ERR_END_OF_STREAM) break;
        if (result != MINI_OK) {
            (void)api->audio->rx->stop(stream);
            (void)api->audio->rx->close(stream);
            return fail(api, "read", 7);
        }
        if (got == 0u) {
            (void)api->audio->rx->stop(stream);
            (void)api->audio->rx->close(stream);
            return fail(api, "zero-frame read", 8);
        }

        uint32_t bytes = got * 2u * (uint32_t)sizeof(int16_t);
        hash = fnv1a_bytes(hash, (const uint8_t *)frames, bytes);
        frame_count += got;
    }

    if (api->audio->rx->stop(stream) != MINI_OK ||
        api->audio->rx->close(stream) != MINI_OK) {
        return fail(api, "stop/close", 9);
    }

    char line[128];
    int written = snprintf(line, sizeof(line),
                           "audio_probe: PASS frames=%llu hash=%016llx\n",
                           (unsigned long long)frame_count,
                           (unsigned long long)hash);
    if (written < 0 || (size_t)written >= sizeof(line)) return 10;
    api->system->write(line);
    return 0;
}

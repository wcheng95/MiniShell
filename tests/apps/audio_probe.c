#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static int16_t abs_s16(int16_t value)
{
    if (value == INT16_MIN) return INT16_MAX;
    return value < 0 ? (int16_t)-value : value;
}

static int parse_seconds(const char *text, uint32_t *out_seconds)
{
    char *end = NULL;
    unsigned long value;

    if (text == NULL || out_seconds == NULL || text[0] == '\0') return 0;
    value = strtoul(text, &end, 10);
    if (end == text || *end != '\0' || value == 0ul || value > 600ul) return 0;
    *out_seconds = (uint32_t)value;
    return 1;
}

int main(int argc, char **argv)
{
    const mini_api_t *api = mini_api_get();
    if (api == NULL || api->api_version != MINISHELL_API_VERSION ||
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
    if (argc < 2 || argc > 4) {
        return fail(api,
                    "usage: audio_probe <endpoint> [seconds [logical-raw-path]]",
                    4);
    }

    uint32_t seconds = 0u;
    int finite_live_capture = 0;
    if (argc >= 3) {
        if (!parse_seconds(argv[2], &seconds))
            return fail(api, "invalid seconds", 4);
        finite_live_capture = 1;
    }

    mini_file_t dump_file = MINI_FILE_INVALID;
    if (argc == 4) {
        if (api->fs == NULL || api->fs->open == NULL || api->fs->write == NULL ||
            api->fs->close == NULL) {
            return fail(api, "filesystem unavailable for dump", 4);
        }
        mini_result_t file_result = api->fs->open(
            argv[3], MINI_FS_WRITE | MINI_FS_CREATE | MINI_FS_TRUNC, &dump_file);
        if (file_result != MINI_OK)
            return fail(api, "dump open", 4);
    }

    mini_audio_format_t format = {
        .struct_size = sizeof(format),
        .sample_rate_hz = 12000u,
        .sample_format = MINI_AUDIO_SAMPLE_S16,
        .channels = 2u,
    };

    mini_audio_stream_t stream = MINI_AUDIO_STREAM_INVALID;
    mini_result_t result = api->audio->rx->open(argv[1], &format, &stream);
    if (result != MINI_OK) {
        if (dump_file != MINI_FILE_INVALID) (void)api->fs->close(dump_file);
        return fail(api, "open", 5);
    }

    result = api->audio->rx->start(stream);
    if (result != MINI_OK) {
        (void)api->audio->rx->close(stream);
        if (dump_file != MINI_FILE_INVALID) (void)api->fs->close(dump_file);
        return fail(api, "start", 6);
    }

    int16_t frames[512u * 2u];
    uint64_t frame_count = 0u;
    uint64_t hash = UINT64_C(14695981039346656037);
    uint64_t sum_abs_l = 0u;
    uint64_t sum_abs_r = 0u;
    uint64_t unequal_lr = 0u;
    int16_t peak_l = 0;
    int16_t peak_r = 0;
    uint64_t start_us = 0u;
    uint64_t end_us = 0u;

    if (finite_live_capture) {
        if (api->time_location == NULL || api->time_location->monotonic_us == NULL) {
            (void)api->audio->rx->stop(stream);
            (void)api->audio->rx->close(stream);
            if (dump_file != MINI_FILE_INVALID) (void)api->fs->close(dump_file);
            return fail(api, "monotonic clock unavailable", 7);
        }
        start_us = api->time_location->monotonic_us();
        end_us = start_us + (uint64_t)seconds * UINT64_C(1000000);
    }

    for (;;) {
        uint32_t got = 0u;
        result = api->audio->rx->read(stream, frames, 512u, &got,
                                     finite_live_capture ? 100u : MINI_WAIT_NONE);
        if (result == MINI_ERR_END_OF_STREAM) break;
        if (result != MINI_OK) {
            (void)api->audio->rx->stop(stream);
            (void)api->audio->rx->close(stream);
            if (dump_file != MINI_FILE_INVALID) (void)api->fs->close(dump_file);
            return fail(api, "read", 8);
        }
        if (got == 0u) {
            if (!finite_live_capture) {
                (void)api->audio->rx->stop(stream);
                (void)api->audio->rx->close(stream);
                if (dump_file != MINI_FILE_INVALID) (void)api->fs->close(dump_file);
                return fail(api, "zero-frame read", 9);
            }
        } else {
            uint32_t bytes = got * 2u * (uint32_t)sizeof(int16_t);
            hash = fnv1a_bytes(hash, (const uint8_t *)frames, bytes);
            frame_count += got;

            for (uint32_t i = 0u; i < got; ++i) {
                int16_t left = frames[i * 2u + 0u];
                int16_t right = frames[i * 2u + 1u];
                int16_t al = abs_s16(left);
                int16_t ar = abs_s16(right);
                if (al > peak_l) peak_l = al;
                if (ar > peak_r) peak_r = ar;
                sum_abs_l += (uint16_t)al;
                sum_abs_r += (uint16_t)ar;
                if (left != right) ++unequal_lr;
            }

            if (dump_file != MINI_FILE_INVALID) {
                uint32_t written = 0u;
                mini_result_t file_result = api->fs->write(
                    dump_file, frames, bytes, &written);
                if (file_result != MINI_OK || written != bytes) {
                    (void)api->audio->rx->stop(stream);
                    (void)api->audio->rx->close(stream);
                    (void)api->fs->close(dump_file);
                    return fail(api, "dump write", 10);
                }
            }
        }

        if (finite_live_capture && api->time_location->monotonic_us() >= end_us)
            break;
    }

    if (api->audio->rx->stop(stream) != MINI_OK ||
        api->audio->rx->close(stream) != MINI_OK) {
        if (dump_file != MINI_FILE_INVALID) (void)api->fs->close(dump_file);
        return fail(api, "stop/close", 11);
    }
    if (dump_file != MINI_FILE_INVALID && api->fs->close(dump_file) != MINI_OK)
        return fail(api, "dump close", 12);

    uint64_t elapsed_us = finite_live_capture
        ? api->time_location->monotonic_us() - start_us
        : 0u;
    uint64_t rate_millihz = elapsed_us > 0u
        ? (frame_count * UINT64_C(1000000000)) / elapsed_us
        : 0u;
    uint64_t mean_abs_l = frame_count > 0u ? sum_abs_l / frame_count : 0u;
    uint64_t mean_abs_r = frame_count > 0u ? sum_abs_r / frame_count : 0u;

    char line[256];
    int written = snprintf(
        line, sizeof(line),
        "audio_probe: PASS frames=%llu rate=%llu.%03lluHz "
        "peak=%d/%d mean_abs=%llu/%llu unequal_lr=%llu hash=%016llx\n",
        (unsigned long long)frame_count,
        (unsigned long long)(rate_millihz / 1000u),
        (unsigned long long)(rate_millihz % 1000u),
        (int)peak_l, (int)peak_r,
        (unsigned long long)mean_abs_l,
        (unsigned long long)mean_abs_r,
        (unsigned long long)unequal_lr,
        (unsigned long long)hash);
    if (written < 0 || (size_t)written >= sizeof(line)) return 13;
    api->system->write(line);
    return 0;
}

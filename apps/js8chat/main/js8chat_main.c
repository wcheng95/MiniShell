#include "js8_live.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#define HAS(p, type, field) ((p) && (p)->struct_size >= offsetof(type, field)+sizeof((p)->field) && (p)->field)

int js8_live_options(int argc, char **argv, Js8LiveOptions *out)
{
    Js8LiveOptions o = {0};
    int have_delay = 0;
    for (int i = 1; i < argc; i += 2) {
        if (i+1 >= argc || !argv[i+1][0]) return -1;
        if (!strcmp(argv[i], "--rx") && !o.rx) o.rx = argv[i+1];
        else if (!strcmp(argv[i], "--cat") && !o.cat) o.cat = argv[i+1];
        else if (!strcmp(argv[i], "--log") && !o.log) o.log = argv[i+1];
        else if (!strcmp(argv[i], "--dial-hz") && !o.have_dial) {
            if (js8_log_parse_dial(argv[i+1], &o.dial_hz)) return -1;
            o.have_dial = 1;
        } else if (!strcmp(argv[i], "--rx-delay-ms") && !have_delay) {
            int64_t value;
            if (js8_log_parse_dial(argv[i+1], &value) || value > 5000) return -1;
            o.rx_delay_ms = (uint32_t)value; have_delay = 1;
        } else if (!strcmp(argv[i], "--slots") && !o.slots) {
            int64_t value;
            if (js8_log_parse_dial(argv[i+1], &value) || value < 1 || value > UINT32_MAX) return -1;
            o.slots = (uint32_t)value;
        } else return -1;
    }
    if (!o.rx || (o.cat && (!o.have_dial || o.dial_hz < 1 || o.dial_hz > UINT32_MAX))) return -1;
    *out = o; return 0;
}
static void say(const mini_api_t *api, const char *s)
{
    if (api && HAS(api->system, mini_system_api_t, write)) api->system->write(s);
}
int js8chat_run(const mini_api_t *api, int argc, char **argv, const Js8Worker *worker)
{
    Js8LiveOptions o;
    if (js8_live_options(argc, argv, &o)) {
        say(api, "usage: js8chat --rx endpoint [--dial-hz hz] [--rx-delay-ms N] [--cat endpoint] [--log path] [--slots N]\n"); return 2;
    }
    if (!api || api->api_version != MINISHELL_API_VERSION || api->struct_size < sizeof(*api) ||
        !worker || !worker->start || !worker->stop ||
        !HAS(api->memory, mini_memory_api_t, alloc) || !HAS(api->memory, mini_memory_api_t, free) ||
        !HAS(api->time_location, mini_time_location_api_t, utc_get) ||
        !HAS(api->time_location, mini_time_location_api_t, monotonic_us) ||
        !HAS(api->time_location, mini_time_location_api_t, sleep_ms) ||
        !(api->time_location->capabilities & MINI_TIMELOC_CAP_UTC) ||
        !HAS(api->audio, mini_audio_api_t, rx) || !(api->audio->capabilities & MINI_AUDIO_CAP_RX) ||
        !HAS(api->audio->rx, mini_audio_rx_api_t, close) || !api->audio->rx->open ||
        !api->audio->rx->start || !api->audio->rx->read || !api->audio->rx->stop ||
        (o.log && (!HAS(api->fs, mini_fs_api_t, sync) || !api->fs->open || !api->fs->close || !api->fs->write))) {
        say(api, "JS8 unavailable services\n"); return 1;
    }
    Js8Live *s = NULL;
    if (api->memory->alloc(sizeof(*s), (void **)&s) || !s) return 1;
    mini_audio_stream_t audio = MINI_AUDIO_STREAM_INVALID;
    mini_serial_t serial = MINI_SERIAL_INVALID;
    int started = 0, working = 0, rc = 1;
    if (js8_live_init(s, api)) goto cleanup;
    s->rx_delay_ms = o.rx_delay_ms;
    s->metadata.have_dial = o.have_dial; s->metadata.dial_hz = o.dial_hz;
    if (o.log && (api->fs->open(o.log, MINI_FS_WRITE|MINI_FS_CREATE|MINI_FS_APPEND, &s->log_file) || !s->log_file)) {
        s->error = "log_open"; goto cleanup;
    }
    if (o.cat && js8_qmx_open(api->serial, o.cat, (uint32_t)o.dial_hz, &serial)) { s->error = "cat"; goto cleanup; }
    mini_audio_format_t format = {.struct_size = sizeof(format), .sample_rate_hz = 12000,
                                  .sample_format = MINI_AUDIO_SAMPLE_S16, .channels = 2};
    if (api->audio->rx->open(o.rx, &format, &audio) || !audio) { s->error = "audio_open"; goto cleanup; }
    if (api->audio->rx->start(audio)) { s->error = "audio_start"; goto cleanup; }
    started = 1;
    if (worker->start(s)) { s->error = "worker_start"; goto cleanup; }
    working = 1;
    say(api, "JS8 receive monitor started (q to quit)\n");
    int eof = 0;
    for (;;) {
        if (js8_live_publish(s)) goto cleanup;
        if (o.slots && s->processed >= o.slots) { rc = 0; break; }
        if (HAS(api->input, mini_input_api_t, key) && (api->input->capabilities & MINI_INPUT_CAP_KEY) &&
            HAS(api->input->key, mini_key_input_api_t, read)) {
            mini_key_event_t key = {.struct_size = sizeof(key)};
            if (!api->input->key->read(&key, MINI_WAIT_NONE) &&
                ((key.type == MINI_KEY_EVENT_CHAR && (key.codepoint == 'q' || key.codepoint == 'Q')) ||
                 (key.type == MINI_KEY_EVENT_SPECIAL && key.key == MINI_KEY_ESCAPE))) { rc = 0; break; }
        }
        if (eof) {
            if (!atomic_load_explicit(&s->job_state, memory_order_acquire)) { rc = o.slots && s->processed < o.slots ? 1 : 0; break; }
            api->time_location->sleep_ms(1); continue;
        }
        int16_t frames[512]; uint32_t got = 0;
        mini_result_t result = api->audio->rx->read(audio, frames, 256, &got, 20);
        if (result == MINI_ERR_DISCONTINUITY) { ++s->discontinuities; js8_live_reset(s); say(api, "JS8 audio discontinuity: resync\n"); continue; }
        if (result == MINI_ERR_END_OF_STREAM) { eof = 1; continue; }
        if (result == MINI_ERR_TIMEOUT) continue;
        if (result || got > 256 || js8_live_chunk(s, frames, got)) { s->error = "audio_timing"; goto cleanup; }
    }
cleanup:
    /* Join before touching worker-owned state or releasing memory/services. */
    if (working) { atomic_fetch_add(&s->generation, 1); worker->stop(s); }
    if (started && api->audio->rx->stop(audio) && !s->error) s->error = "audio_stop";
    if (audio && api->audio->rx->close(audio) && !s->error) s->error = "audio_close";
    if (serial && api->serial->close(serial) && !s->error) s->error = "cat_close";
    js8_live_destroy(s);
    char report[160];
    snprintf(report, sizeof(report), "JS8 stopped slots=%u drops=%u discontinuities=%u error=%s\n",
             s->processed, s->dropped, s->discontinuities, s->error ? s->error : "none");
    say(api, report);
    if (s->error) rc = 1;
    api->memory->free(s);
    return rc;
}

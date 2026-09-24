#include "js8_live.h"
#include <stdio.h>
#include <string.h>

static uint64_t now(Js8Live *s) { return s->api->time_location->monotonic_us(); }
static void diagnostic(Js8Live *s, const char *event, uint32_t slot)
{
    char line[224];
    int done = atomic_load_explicit(&s->job_state, memory_order_acquire) == 2;
    snprintf(line, sizeof(line), "JS8 %s slot=%u decode_us=%llu read_gap_max_us=%llu candidates=%u unique=%u drops=%u discontinuities=%u\n",
             event, slot, (unsigned long long)(done ? s->decode_us : 0), (unsigned long long)s->max_read_gap_us,
             (unsigned)(done ? s->candidate_count : 0), (unsigned)(done ? s->decoded_count : 0), s->dropped, s->discontinuities);
    if (s->api->system && s->api->system->write) s->api->system->write(line);
}
int js8_live_init(Js8Live *s, const mini_api_t *api)
{
    memset(s, 0, sizeof(*s)); s->api = api;
    atomic_init(&s->job_state, 0); atomic_init(&s->generation, 0);
    Js8MonitorConfig cfg = js8_monitor_baseline_config();
    Js8MonitorRequirements r;
    if (js8_monitor_query_requirements(&cfg, &r) || r.total_bytes+r.alignment-1 > UINT32_MAX) return -1;
    if (api->memory->alloc((uint32_t)(r.total_bytes+r.alignment-1), &s->monitor_allocation) || !s->monitor_allocation) return -1;
    uintptr_t aligned = ((uintptr_t)s->monitor_allocation+r.alignment-1) & ~(uintptr_t)(r.alignment-1);
    if (js8_monitor_init(&s->monitor, &cfg, (void *)aligned, r.total_bytes)) return -1;
    if (api->memory->alloc((uint32_t)r.waterfall_bytes, &s->job_allocation) || !s->job_allocation) return -1;
    s->metadata.have_utc = 1;
    s->metadata.start_seconds = UINT64_C(62135596800); /* Unix epoch in shared Gregorian origin. */
    return 0;
}
void js8_live_destroy(Js8Live *s)
{
    if (s->log_file && s->api->fs->close(s->log_file) && !s->error) s->error = "log_close";
    if (s->dictionary_file && s->api->fs->close(s->dictionary_file) && !s->error) s->error = "dictionary_close";
    js8_monitor_destroy(&s->monitor);
    if (s->job_allocation) s->api->memory->free(s->job_allocation);
    if (s->monitor_allocation) s->api->memory->free(s->monitor_allocation);
}
void js8_live_reset(Js8Live *s)
{
    atomic_fetch_add_explicit(&s->generation, 1, memory_order_release);
    memset(&s->frontend, 0, sizeof(s->frontend));
    memset(&s->scheduler, 0, sizeof(s->scheduler)); s->block_fill = 0;
    js8_monitor_reset_stream(&s->monitor);
    js8_rx_reassembly_init(&s->reassembly);
    /* Worker owns its snapshot until DONE; old-generation output is discarded. */
}
static int slot_event(void *context, Js8SlotEvent event, uint32_t slot, const float *samples, size_t count)
{
    Js8Live *s = context;
    if (event == JS8_SLOT_BEGIN) {
        js8_monitor_reset_stream(&s->monitor); s->block_fill = 0;
    } else if (event == JS8_SLOT_DROP) {
        ++s->dropped; diagnostic(s, "timing-drop", slot);
    } else if (event == JS8_SLOT_SAMPLES) {
        while (count) {
            size_t n = 960-s->block_fill; if (n > count) n = count;
            memcpy(s->block+s->block_fill, samples, n*sizeof(float));
            s->block_fill += n; samples += n; count -= n;
            if (s->block_fill == 960) {
                if (js8_monitor_process_block(&s->monitor, s->block)) return -1;
                s->block_fill = 0;
            }
        }
    } else {
        if (atomic_load_explicit(&s->job_state, memory_order_acquire) != 0) {
            ++s->dropped; diagnostic(s, "decode-busy-drop", slot); return 0;
        }
        if (s->block_fill || s->monitor.num_blocks != 93 || js8_monitor_get_waterfall(&s->monitor, &s->job_view)) return -1;
        memcpy(s->job_allocation, s->job_view.mag, s->monitor.req.waterfall_bytes);
        s->job_view.mag = s->job_allocation; s->job_slot = slot;
        s->job_generation = atomic_load_explicit(&s->generation, memory_order_acquire);
        atomic_store_explicit(&s->job_state, 1, memory_order_release);
    }
    return 0;
}
int js8_live_chunk(Js8Live *s, const int16_t *frames, size_t count)
{
    float samples[128]; size_t produced = 0;
    if (count > 256 || js8_frontend_process(&s->frontend, frames, count, samples, 128, &produced)) return -1;
    mini_utc_time_t utc = {.struct_size = sizeof(utc)};
    /* Every successful conversion is freshly UTC-referenced, even after startup. */
    if (s->api->time_location->utc_get(&utc) != MINI_OK) { js8_live_reset(s); return 0; }
    if (js8_live_delay(&utc.unix_seconds, &utc.nanoseconds, s->rx_delay_ms)) return -1;
    int64_t slot; uint32_t offset;
    if (js8_live_anchor(utc.unix_seconds, utc.nanoseconds, produced, &slot, &offset) || slot < 0 || slot > UINT32_MAX) return -1;
    uint64_t stamp = now(s);
    if (s->last_read_us && stamp >= s->last_read_us && stamp-s->last_read_us > s->max_read_gap_us)
        s->max_read_gap_us = stamp-s->last_read_us;
    s->last_read_us = stamp;
    return js8_slot_feed(&s->scheduler, (uint32_t)slot, offset, samples, produced, slot_event, s);
}
int js8_live_decode_step(Js8Live *s)
{
    if (atomic_load_explicit(&s->job_state, memory_order_acquire) != 1) return 0;
    uint64_t start = now(s);
    Js8Candidate candidates[JS8_DECODER_CANDIDATE_CAPACITY];
    s->decoded_count = 0;
    s->decode_error = js8_decoder_find_candidates(&s->job_view, candidates, JS8_DECODER_CANDIDATE_CAPACITY,
                                                 JS8_DECODER_MIN_SCORE, &s->candidate_count);
    if (!s->decode_error) for (size_t i = 0; i < s->candidate_count; ++i) {
        if (s->job_generation != atomic_load_explicit(&s->generation, memory_order_acquire)) break;
        Js8DecodedPayload payload;
        Js8DecoderStatus status = js8_decoder_decode_candidate(&s->job_view, &candidates[i], &payload);
        if (status == JS8_DECODER_ERR_LDPC || status == JS8_DECODER_ERR_CRC) continue;
        if (status != JS8_DECODER_OK) { s->decode_error = -1; break; }
        size_t j;
        for (j = 0; j < s->decoded_count; ++j)
            if (!memcmp(payload.payload_bits, s->decoded[j].payload_bits, JS8_PAYLOAD_BITS)) break;
        if (j == s->decoded_count) s->decoded[s->decoded_count++] = payload;
    }
    s->decode_us = now(s)-start;
    atomic_store_explicit(&s->job_state, 2, memory_order_release);
    return 1;
}
int js8_live_publish(Js8Live *s)
{
    if (atomic_load_explicit(&s->job_state, memory_order_acquire) != 2) return 0;
    int result = 0;
    if (s->job_generation == atomic_load_explicit(&s->generation, memory_order_acquire)) {
        if (s->decode_error) result = -1;
        for (size_t i = 0; !result && i < s->decoded_count; ++i)
            result = js8_live_payload(s, s->job_slot, &s->decoded[i]);
        if (s->log_file && s->api->fs->sync(s->log_file)) { s->error = "log_sync"; result = -1; }
        ++s->processed; diagnostic(s, "decoded", s->job_slot);
    }
    atomic_store_explicit(&s->job_state, 0, memory_order_release);
    return result;
}

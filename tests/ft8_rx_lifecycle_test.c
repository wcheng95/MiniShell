#include <assert.h>
#include <stdlib.h>
#define FT8_APP_CONTROLLER_INTERNAL 1
#define FT8_DECODE_DIAGNOSTICS 1
#include "../apps/ft8/src/app_controller/app_controller.c"

static int64_t position;
static char diagnostics[32768];
static size_t diag_used;
static mini_result_t read_result;

static mini_result_t mem_alloc(uint32_t n, void **out)
{ *out = malloc(n); return *out ? MINI_OK : MINI_ERR_NO_MEMORY; }
static mini_result_t mem_free(void *p) { free(p); return MINI_OK; }
static mini_result_t audio_open(const char *endpoint, const mini_audio_format_t *fmt,
                                mini_audio_stream_t *out)
{ (void)endpoint; (void)fmt; *out = 1; return MINI_OK; }
static mini_result_t audio_state(mini_audio_stream_t stream)
{ assert(stream == 1); return MINI_OK; }
static mini_result_t audio_read(mini_audio_stream_t stream, void *frames, uint32_t cap,
                                uint32_t *got, uint32_t timeout)
{ (void)stream; (void)frames; (void)cap; (void)timeout; *got = 0; return read_result; }
static uint64_t monotonic_us(void) { return (uint64_t)position * 1000u / 6u; }
static mini_result_t utc_get(mini_utc_time_t *utc)
{
    utc->unix_seconds = position / 6000;
    utc->nanoseconds = (uint32_t)(position % 6000) * 1000000000ull / 6000;
    return MINI_OK;
}
static void diag_write(const char *line)
{
    size_t n = strlen(line);
    assert(diag_used + n < sizeof(diagnostics));
    memcpy(diagnostics + diag_used, line, n + 1);
    diag_used += n;
}

static const mini_memory_api_t memory = {.alloc = mem_alloc, .free = mem_free};
static const mini_audio_rx_api_t audio_rx = {
    .struct_size = sizeof(audio_rx), .open = audio_open, .start = audio_state,
    .stop = audio_state, .close = audio_state, .read = audio_read
};
static const mini_audio_api_t audio = {
    .struct_size = sizeof(audio), .capabilities = MINI_AUDIO_CAP_RX, .rx = &audio_rx
};
static const mini_time_location_api_t time_api = {
    .capabilities = MINI_TIMELOC_CAP_UTC, .utc_get = utc_get, .monotonic_us = monotonic_us
};
static const mini_system_api_t system_api = {.write = diag_write};
static const mini_api_t api = {
    .memory = &memory, .audio = &audio, .time_location = &time_api, .system = &system_api
};

static int64_t boundary_at(int64_t slot) { return slot * 90000; }
static int64_t decode_at(int64_t slot) { return boundary_at(slot) + 75840; }

static void start(AppController *app)
{
    memset(app, 0, sizeof(*app));
    app->api = &api;
    assert(auto_seq_init(&app->auto_seq, NULL));
    AppRxStartConfig config = {.endpoint = "fake"};
    assert(app_controller_start_rx(app, &config));
    assert(app_controller_enable_decode_worker(app, true));

    /* Bypass the public read path only to drive the production live scheduler
     * with exact 6-kHz positions. */
    assert(rx_slot_framer_init(&app->rx->framer, 100, 0) == RX_SLOT_FRAMER_OK);
    app->rx->framer_initialized = true;
    app->rx->timing_pending = false;
    position = boundary_at(100);
    read_result = MINI_OK;
    diagnostics[0] = '\0';
    diag_used = 0;
}

static void stop(AppController *app)
{
    app_controller_decode_worker_abort(app);
    assert(app_controller_enable_decode_worker(app, false));
    app_rx_destroy(app);
}

/* Actual scheduler + framer + monitor, using precise 20-ms UTC chunks. */
static bool feed_until(AppController *app, int64_t end)
{
    static const float silence[120] = {0};
    while (position < end) {
        int64_t first = position;
        size_t n = end - position < 120 ? (size_t)(end - position) : 120;
        position += (int64_t)n;
        if (!rx_live_process_timed_samples(app->rx, silence, n,
                                           first / 90000, (uint32_t)(first % 90000)))
            return false;
    }
    return true;
}

static void finish_decode(AppController *app)
{
    unsigned steps = 0;
    while (rx_decode_state(app->rx) == RX_DECODE_ASYNC_RUNNING) {
        bool work = false;
        assert(++steps < 100);
        assert(app_controller_decode_worker_step(app, &work) && work);
    }
    assert(rx_decode_state(app->rx) == RX_DECODE_ASYNC_RESULT);
    assert(!ft8_engine_decode_active(&app->rx->engine));
    assert(app->rx->decode_completed_slot.messages == app->rx->protocol_messages);
}

static void test_consecutive_slot_local_capture(void)
{
    AppController app;
    start(&app);
    AppRxState *rx = app.rx;

    for (int64_t slot = 100; slot < 105; ++slot) {
        assert(feed_until(&app, decode_at(slot)));
        assert(rx->framer.capture_slot_id == slot);
        assert(rx->framer.capture_block_count == RX_SLOT_FRAMER_DECODE_BLOCKS);
        assert(rx->engine.monitor.num_blocks == RX_SLOT_FRAMER_DECODE_BLOCKS);
        assert(rx->engine.slot_id == slot);
        assert(rx->engine.slot_anchor_seq == 0u);
        assert(rx->engine.decode_slot_id == slot);
        assert(!rx->live_capture_active);
        assert(rx_decode_state(rx) == RX_DECODE_ASYNC_RUNNING);

        finish_decode(&app);
        uint64_t generation = rx->batch_generation;
        assert(app_publish_external_decode(&app));
        assert(rx->batch.slot_id == slot && rx->batch.message_count == 0);
        assert(rx->batch_generation == generation + 1);
        assert(rx_decode_state(rx) == RX_DECODE_ASYNC_IDLE);
    }

    const char *cursor = diagnostics;
    for (int slot = 100; slot < 105; ++slot) {
        const char *events[] = {"CAPTURE_RESET", "DECODE_START",
                                "DECODE_DONE", "RESULT_PUBLISH"};
        for (unsigned i = 0; i < 4; ++i) {
            char match[80];
            snprintf(match, sizeof(match), "FT8D %s slot=%d ", events[i], slot);
            cursor = strstr(cursor, match);
            assert(cursor);
            ++cursor;
        }
    }
    assert(!strstr(diagnostics, "INVARIANT"));
    stop(&app);
}

static void test_decode_may_cross_boundary(void)
{
    AppController app;
    start(&app);
    AppRxState *rx = app.rx;

    assert(feed_until(&app, decode_at(100)));
    assert(rx_decode_state(rx) == RX_DECODE_ASYNC_RUNNING);
    Ft8WaterfallView old_view = rx->engine.decode_search_waterfall;

    /* Slot 101 capture takes ownership exactly at UTC while decode 100 runs. */
    assert(feed_until(&app, boundary_at(101)));
    assert(rx->framer.capture_slot_id == 101);
    assert(rx->engine.slot_id == 101);
    assert(rx->engine.monitor.num_blocks == 0u);
    assert(rx->engine.slot_anchor_seq == 0u);
    assert(rx_decode_state(rx) == RX_DECODE_ASYNC_RUNNING);
    assert(ft8_engine_decode_active(&rx->engine));
    assert(memcmp(&old_view, &rx->engine.decode_search_waterfall,
                  sizeof(old_view)) == 0);

    /* New waterfall production continues independently. */
    assert(feed_until(&app, boundary_at(101) + 10 * 960));
    assert(rx->engine.monitor.num_blocks == 10u);
    assert(rx_decode_state(rx) == RX_DECODE_ASYNC_RUNNING);

    finish_decode(&app);
    assert(rx->decode_completed_slot.slot_id == 100);
    assert(app_publish_external_decode(&app));

    assert(feed_until(&app, decode_at(101)));
    assert(rx->engine.monitor.num_blocks == RX_SLOT_FRAMER_DECODE_BLOCKS);
    assert(rx->engine.decode_slot_id == 101);
    finish_decode(&app);
    assert(app_publish_external_decode(&app));
    assert(!strstr(diagnostics, "INVARIANT"));
    stop(&app);
}

static void test_gap_does_not_move_deadlines(void)
{
    AppController app;
    start(&app);
    AppRxState *rx = app.rx;

    assert(feed_until(&app, boundary_at(100) + 20000));
    assert(rx->live_capture_active);
    unsigned before = rx->engine.monitor.num_blocks;

    read_result = MINI_ERR_DISCONTINUITY;
    bool changed = false;
    assert(app_controller_step_rx(&app, &changed));
    assert(!changed && !rx->timing_pending && rx->live_capture_active);
    read_result = MINI_OK;

    /* Simulate missing transport time. UTC, not sample count, still owns decode. */
    position += 2400;
    assert(feed_until(&app, decode_at(100)));
    assert(rx->engine.decode_slot_id == 100);
    assert(rx->engine.monitor.num_blocks < RX_SLOT_FRAMER_DECODE_BLOCKS);
    assert(rx->engine.monitor.num_blocks >= before);
    finish_decode(&app);
    assert(app_publish_external_decode(&app));

    /* The next undamaged slot starts cleanly and has exactly 79 blocks. */
    assert(feed_until(&app, decode_at(101)));
    assert(rx->engine.monitor.num_blocks == RX_SLOT_FRAMER_DECODE_BLOCKS);
    assert(rx->engine.decode_slot_id == 101);
    finish_decode(&app);
    assert(app_publish_external_decode(&app));
    stop(&app);
}

static void test_stale_decode_current_slot_wins(bool ready)
{
    AppController app;
    start(&app);
    AppRxState *rx = app.rx;

    assert(feed_until(&app, decode_at(100)));
    if (ready)
        finish_decode(&app);

    uint64_t generation = rx->batch_generation;
    assert(feed_until(&app, decode_at(101)));
    assert(rx->engine.monitor.num_blocks == RX_SLOT_FRAMER_DECODE_BLOCKS);

    if (ready) {
        /* An unconsumed old result is dropped immediately at the new trigger. */
        assert(rx_decode_state(rx) == RX_DECODE_ASYNC_RUNNING);
        assert(rx->engine.decode_slot_id == 101);
        assert(!rx->live_decode_pending);
        assert(rx->batch_generation == generation);
        assert(strstr(diagnostics, "FT8D OVERRUN drop-result slot=101 prior_slot=100"));
    } else {
        /* Core 0 requests cancel; core 1 owns cancellation, then pending 101 starts. */
        assert(rx_decode_state(rx) == RX_DECODE_ASYNC_CANCEL_REQUESTED);
        assert(rx->live_decode_pending && rx->live_decode_pending_slot == 101);
        bool work = false;
        assert(app_controller_decode_worker_step(&app, &work) && work);
        assert(rx_decode_state(rx) == RX_DECODE_ASYNC_CANCELED);
        assert(app_publish_external_decode(&app));
        assert(rx_decode_state(rx) == RX_DECODE_ASYNC_RUNNING);
        assert(rx->engine.decode_slot_id == 101);
        assert(!rx->live_decode_pending);
        assert(strstr(diagnostics, "FT8D OVERRUN cancel-pending slot=101 prior_slot=100"));
    }

    finish_decode(&app);
    assert(rx->decode_completed_slot.slot_id == 101);
    assert(app_publish_external_decode(&app));
    assert(rx->batch.slot_id == 101);
    assert(rx->batch_generation == generation + 1);
    assert(!strstr(diagnostics, "INVARIANT"));
    stop(&app);
}

static void test_start_mid_slot_waits_for_complete_slot(void)
{
    AppController app;
    start(&app);
    AppRxState *rx = app.rx;

    position = boundary_at(100) + 6000; /* one second late */
    rx->live_capture_schedule_valid = false;
    rx->live_capture_active = false;
    assert(feed_until(&app, boundary_at(101)));
    assert(rx->framer.capture_slot_id == 101);
    assert(rx->engine.slot_id == 101);
    assert(rx->engine.monitor.num_blocks == 0u);

    assert(feed_until(&app, decode_at(101)));
    assert(rx->engine.monitor.num_blocks == RX_SLOT_FRAMER_DECODE_BLOCKS);
    assert(rx->engine.decode_slot_id == 101);
    finish_decode(&app);
    assert(app_publish_external_decode(&app));
    stop(&app);
}

static void test_prompt_publication(void)
{
    AppController app;
    start(&app);
    assert(feed_until(&app, decode_at(100)));
    finish_decode(&app);

    uint64_t generation = app.rx->batch_generation;
    read_result = MINI_ERR_TIMEOUT;
    bool changed = false;
    assert(app_controller_step_rx(&app, &changed) && changed);
    assert(app.rx->batch_generation == generation + 1);
    assert(app.rx->have_applied_batch && app.rx->applied_slot == 100);
    assert(rx_decode_state(app.rx) == RX_DECODE_ASYNC_IDLE);
    stop(&app);
}

int main(void)
{
    test_consecutive_slot_local_capture();
    test_decode_may_cross_boundary();
    test_gap_does_not_move_deadlines();
    test_stale_decode_current_slot_wins(false);
    test_stale_decode_current_slot_wins(true);
    test_start_mid_slot_waits_for_complete_slot();
    test_prompt_publication();
    puts("FT8 slot-local live RX lifecycle: PASS");
    return 0;
}

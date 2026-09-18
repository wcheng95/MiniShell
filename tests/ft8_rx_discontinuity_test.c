#include <assert.h>
#include <stdlib.h>
/* White-box test of production controller and its actual RX/DSP modules. */
#define FT8_APP_CONTROLLER_INTERNAL 1
#include "../apps/ft8/src/app_controller/app_controller.c"

#ifndef FT8_TEST_EXPECT_FREQ_OSR
#define FT8_TEST_EXPECT_FREQ_OSR 2
#endif

static mini_result_t read_result = MINI_OK;
static bool utc_available = true;
static unsigned opens, starts, stops, closes;
static mini_result_t mem_alloc(uint32_t size, void **out)
{ *out = malloc(size); return *out ? MINI_OK : MINI_ERR_NO_MEMORY; }
static mini_result_t mem_free(void *ptr) { free(ptr); return MINI_OK; }
static mini_result_t audio_open(const char *endpoint, const mini_audio_format_t *format,
                                mini_audio_stream_t *out)
{ (void)endpoint; (void)format; ++opens; *out = 1; return MINI_OK; }
static mini_result_t audio_start(mini_audio_stream_t stream)
{ assert(stream == 1); ++starts; return MINI_OK; }
static mini_result_t audio_stop(mini_audio_stream_t stream)
{ assert(stream == 1); ++stops; return MINI_OK; }
static mini_result_t audio_close(mini_audio_stream_t stream)
{ assert(stream == 1); ++closes; return MINI_OK; }
static mini_result_t audio_read(mini_audio_stream_t stream, void *frames, uint32_t capacity,
                                uint32_t *got, uint32_t timeout)
{
    (void)timeout; assert(stream == 1);
    memset(frames, 0, capacity * 2 * sizeof(int16_t));
    *got = capacity;
    return read_result;
}
static mini_result_t utc_get(mini_utc_time_t *utc)
{
    utc->unix_seconds = 1505; /* slot 100, offset 30000 */
    utc->nanoseconds = 0;
    return utc_available ? MINI_OK : MINI_ERR_NOT_READY;
}
int main(void)
{
    const mini_memory_api_t memory = {.alloc = mem_alloc, .free = mem_free};
    const mini_audio_rx_api_t audio_rx = {
        .struct_size = sizeof(audio_rx), .open = audio_open, .start = audio_start,
        .read = audio_read, .stop = audio_stop, .close = audio_close
    };
    const mini_audio_api_t audio = {
        .struct_size = sizeof(audio), .capabilities = MINI_AUDIO_CAP_RX, .rx = &audio_rx
    };
    const mini_time_location_api_t time = {
        .capabilities = MINI_TIMELOC_CAP_UTC, .utc_get = utc_get
    };
    const mini_api_t api = {.memory = &memory, .audio = &audio, .time_location = &time};
    AppController app = {.api = &api};
    assert(auto_seq_init(&app.auto_seq, NULL));
    AppRxStartConfig config = {.endpoint = "fake", .has_explicit_timing = true, .slot_id = 1};
    assert(app_controller_start_rx(&app, &config));
    Ft8EngineConfig baseline = ft8_engine_baseline_config();
    assert(baseline.monitor.time_osr == 2 && baseline.monitor.freq_osr == 2);
    assert(app.rx->engine.config.monitor.time_osr == 2);
    assert(app.rx->engine.config.monitor.freq_osr == FT8_TEST_EXPECT_FREQ_OSR);
    Ft8EngineRequirements requirements;
    assert(ft8_engine_query_requirements(&app.rx->engine.config, &requirements) == FT8_ENGINE_OK);
    printf("controller time_osr=2 freq_osr=%u workspace=%zu alignment=%zu\n",
           app.rx->engine.config.monitor.freq_osr, requirements.workspace_bytes, requirements.alignment);
    bool changed;
    assert(app_controller_step_rx(&app, &changed));
    AppRxState *rx = app.rx;
    assert(rx->frontend.decimation_phase == 1 && rx->framer.block_fill == 129);
    assert(rx->engine.window_active);
    rx->have_batch = true;
    rx->batch_generation = 7;
    RxBatch historical = rx->batch;
    assert(ft8_hash_store_save(&rx->engine.hash_store, "AG6AQ", 12345) == FT8_HASH_STORE_OK);
    Ft8HashStore hashes = rx->engine.hash_store;
    rx->engine.monitor.history[0] = 0.5f;
    read_result = MINI_ERR_DISCONTINUITY;
    assert(app_controller_step_rx(&app, &changed));
    assert(!changed && rx->active && rx->timing_pending && rx->framer_initialized);
    assert(rx->frontend.decimation_phase == 0);
    assert(app_controller_step_rx(&app, &changed)); /* repeated gap before data */
    read_result = MINI_OK;
    assert(app_controller_step_rx(&app, &changed));
    assert(rx->framer.slot_id == 100 && rx->framer.sample_offset == 30000);
    assert(rx->framer.waiting_for_full_boundary && rx->framer.block_fill == 0);
    assert(!rx->timing_pending && !rx->engine.window_active);
    assert(rx->engine.monitor.history[0] == 0.0f);
    assert(memcmp(&hashes, &rx->engine.hash_store, sizeof(hashes)) == 0);
    assert(rx->have_batch && rx->batch_generation == 7);
    assert(memcmp(&historical, &rx->batch, sizeof(historical)) == 0);
    assert(opens == 1 && starts == 1 && stops == 0 && closes == 0);
    unsigned steps = 0;
    while (rx->batch_generation == 7) {
        assert(++steps < 2000);
        assert(app_controller_step_rx(&app, &changed));
        if (rx->framer.slot_id == 100) assert(!rx->engine.window_active);
    }
    assert(rx->framer.slot_id == 101 && rx->framer.decode_emitted);
    assert(rx->batch_generation == 8); /* first subsequent complete window decoded */
    read_result = MINI_ERR_DISCONTINUITY;
    assert(app_controller_step_rx(&app, &changed));
    read_result = MINI_OK;
    utc_available = false;
    assert(!app_controller_step_rx(&app, &changed));
    app_rx_destroy(&app);

    /* A gap before first timing establishment must use init, not reset. */
    config.has_explicit_timing = false;
    utc_available = true;
    assert(app_controller_start_rx(&app, &config));
    read_result = MINI_ERR_DISCONTINUITY;
    assert(app_controller_step_rx(&app, &changed));
    assert(!app.rx->framer_initialized);
    read_result = MINI_OK;
    assert(app_controller_step_rx(&app, &changed));
    assert(app.rx->framer_initialized && app.rx->framer.slot_id == 100);
    app_rx_destroy(&app);
    puts("controller RX discontinuity: PASS");
    return 0;
}

#include "app_controller.h"

#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "ft8_engine.h"
#include "rx_audio_adapter.h"
#include "rx_frontend.h"
#include "rx_result_builder.h"
#include "rx_slot_framer.h"

#define RX_TRANSPORT_FRAMES 257u
#define RX_FRONTEND_OUT_CAPACITY ((RX_TRANSPORT_FRAMES + 1u) / 2u)

struct AppRxState {
    RxAudioAdapter audio;
    RxFrontend frontend;
    RxSlotFramer framer;
    Ft8Engine engine;
    RxResultBuilder builder;

    Ft8ProtocolMessage protocol_messages[FT8_DECODER_CANDIDATE_CAPACITY];
    RxMessage rx_messages[FT8_DECODER_CANDIDATE_CAPACITY];
    RxBatch batch;

    void *engine_allocation;
    uint32_t engine_allocation_bytes;

    int16_t transport_frames[RX_TRANSPORT_FRAMES * RX_AUDIO_ADAPTER_CHANNELS];
    float frontend_samples[RX_FRONTEND_OUT_CAPACITY];

    bool engine_initialized;
    bool frontend_initialized;
    bool framer_initialized;
    bool builder_initialized;
    bool audio_initialized;
    bool active;
    bool have_batch;
    uint64_t batch_generation;
};

static void copy_ui_text(char out[UI_TEXT_CAP], const char *text)
{
    size_t length = strlen(text);
    if (length >= UI_TEXT_CAP) length = UI_TEXT_CAP - 1u;
    memcpy(out, text, length);
    out[length] = '\0';
}

static bool app_save_config(AppController *app)
{
    char text[2048];
    if (!config_service_serialize(&app->config, text, sizeof(text))) return false;
    return storage_service_write_text_atomic(&app->storage, app->station_path, text);
}

static void app_rx_destroy(AppController *app)
{
    AppRxState *rx;
    if (app == NULL || app->rx == NULL) return;
    rx = app->rx;

    if (rx->audio_initialized && rx->audio.open) {
        (void)rx_audio_adapter_close(&rx->audio);
    }
    if (rx->builder_initialized) rx_result_builder_destroy(&rx->builder);
    if (rx->framer_initialized) rx_slot_framer_destroy(&rx->framer);
    if (rx->frontend_initialized) rx_frontend_destroy(&rx->frontend);
    if (rx->engine_initialized) ft8_engine_destroy(&rx->engine);
    if (rx->engine_allocation != NULL && app->api != NULL &&
        app->api->memory != NULL && app->api->memory->free != NULL) {
        (void)app->api->memory->free(rx->engine_allocation);
    }
    if (app->api != NULL && app->api->memory != NULL && app->api->memory->free != NULL) {
        (void)app->api->memory->free(rx);
    }
    app->rx = NULL;
}

static bool utc_to_slot_reference(const mini_time_location_api_t *time_location,
                                  int64_t *out_slot_id,
                                  uint32_t *out_sample_offset)
{
    mini_utc_time_t utc = {.struct_size = sizeof(utc)};
    int64_t slot_id;
    int64_t second_in_slot;
    uint64_t fractional_samples;

    if (time_location == NULL || out_slot_id == NULL || out_sample_offset == NULL ||
        (time_location->capabilities & MINI_TIMELOC_CAP_UTC) == 0u ||
        time_location->utc_get == NULL || time_location->utc_get(&utc) != MINI_OK ||
        utc.nanoseconds >= 1000000000u) {
        return false;
    }

    slot_id = utc.unix_seconds / (int64_t)RX_SLOT_FRAMER_SLOT_SECONDS;
    second_in_slot = utc.unix_seconds % (int64_t)RX_SLOT_FRAMER_SLOT_SECONDS;
    if (second_in_slot < 0) {
        second_in_slot += RX_SLOT_FRAMER_SLOT_SECONDS;
        --slot_id;
    }

    fractional_samples = ((uint64_t)utc.nanoseconds * RX_SLOT_FRAMER_SAMPLE_RATE_HZ) /
                         1000000000ull;
    *out_slot_id = slot_id;
    *out_sample_offset = (uint32_t)(second_in_slot * RX_SLOT_FRAMER_SAMPLE_RATE_HZ) +
                         (uint32_t)fractional_samples;
    return *out_sample_offset < RX_SLOT_FRAMER_SLOT_SAMPLES;
}

static int rx_emit_event(void *ctx, const RxSlotFramerEvent *event)
{
    AppRxState *rx = (AppRxState *)ctx;

    if (rx == NULL || event == NULL) return -1;

    switch (event->type) {
    case RX_SLOT_FRAMER_EVENT_BEGIN_WINDOW:
        return ft8_engine_begin_window(&rx->engine, event->slot_id) == FT8_ENGINE_OK ? 0 : -1;

    case RX_SLOT_FRAMER_EVENT_ENGINE_BLOCK:
        return ft8_engine_process_block(&rx->engine, event->samples) == FT8_ENGINE_OK ? 0 : -1;

    case RX_SLOT_FRAMER_EVENT_FINALIZE_WINDOW: {
        Ft8ProtocolSlot slot;
        Ft8EngineStatus engine_status = ft8_engine_finalize_window(
            &rx->engine, rx->protocol_messages, FT8_DECODER_CANDIDATE_CAPACITY, &slot);
        RxResultStatus result_status;

        if (engine_status != FT8_ENGINE_OK && engine_status != FT8_ENGINE_NO_MESSAGES) return -1;
        result_status = rx_result_builder_build(&rx->builder, &slot,
                                                rx->rx_messages,
                                                FT8_DECODER_CANDIDATE_CAPACITY,
                                                &rx->batch);
        if (result_status != RX_RESULT_OK) return -1;
        rx->have_batch = true;
        ++rx->batch_generation;
        return 0;
    }

    case RX_SLOT_FRAMER_EVENT_STREAM_RESET:
        return ft8_engine_reset_stream(&rx->engine) == FT8_ENGINE_OK ? 0 : -1;
    }

    return -1;
}

bool app_controller_init(AppController *app, const mini_api_t *api,
                         const char *data_directory, const char *station_path)
{
    if (app == NULL || api == NULL || api->fs == NULL ||
        data_directory == NULL || station_path == NULL) {
        return false;
    }
    memset(app, 0, sizeof(*app));
    app->api = api;

    if (!storage_service_init(&app->storage, api->fs)) return false;
    if (!storage_service_ensure_directory(&app->storage, data_directory)) return false;

    int path_length = snprintf(app->station_path, sizeof(app->station_path), "%s", station_path);
    if (path_length < 0 || (size_t)path_length >= sizeof(app->station_path)) return false;

    config_service_defaults(&app->config);
    qso_scheduler_init(&app->scheduler);

    char text[2048];
    bool loaded = storage_service_read_text(&app->storage, app->station_path,
                                            text, sizeof(text));
    if (loaded && !config_service_parse(&app->config, text)) return false;

    qso_scheduler_set_skip_tx1(&app->scheduler, app->config.skip_tx1);
    qso_scheduler_set_max_retry(&app->scheduler, app->config.max_retry);

    if (!loaded && !app_save_config(app)) return false;
    return true;
}

bool app_controller_start_rx(AppController *app, const AppRxStartConfig *config)
{
    AppRxState *rx = NULL;
    Ft8EngineConfig engine_config;
    Ft8EngineRequirements req;
    RxFrontendConfig frontend_config;
    RxResultBuilderConfig builder_config;
    void *workspace;
    uintptr_t raw;
    uintptr_t aligned;
    int64_t slot_id;
    uint32_t sample_offset;

    if (app == NULL || app->api == NULL || config == NULL || config->endpoint == NULL ||
        app->rx != NULL || app->api->memory == NULL || app->api->memory->alloc == NULL ||
        app->api->memory->free == NULL || app->api->audio == NULL) {
        return false;
    }

    if (config->has_explicit_timing) {
        slot_id = config->slot_id;
        sample_offset = config->sample_offset;
        if (sample_offset >= RX_SLOT_FRAMER_SLOT_SAMPLES) return false;
    } else if (!utc_to_slot_reference(app->api->time_location, &slot_id, &sample_offset)) {
        return false;
    }

    if (sizeof(*rx) > UINT32_MAX ||
        app->api->memory->alloc((uint32_t)sizeof(*rx), (void **)&rx) != MINI_OK || rx == NULL) {
        return false;
    }
    memset(rx, 0, sizeof(*rx));
    app->rx = rx;

    engine_config = ft8_engine_baseline_config();
    if (ft8_engine_query_requirements(&engine_config, &req) != FT8_ENGINE_OK ||
        req.workspace_bytes > UINT32_MAX || req.alignment == 0u ||
        (req.alignment & (req.alignment - 1u)) != 0u ||
        req.alignment - 1u > UINT32_MAX - req.workspace_bytes) {
        goto fail;
    }
    rx->engine_allocation_bytes = (uint32_t)(req.workspace_bytes + req.alignment - 1u);
    if (app->api->memory->alloc(rx->engine_allocation_bytes, &rx->engine_allocation) != MINI_OK ||
        rx->engine_allocation == NULL) {
        goto fail;
    }
    raw = (uintptr_t)rx->engine_allocation;
    aligned = (raw + req.alignment - 1u) & ~((uintptr_t)req.alignment - 1u);
    workspace = (void *)aligned;

    if (ft8_engine_init(&rx->engine, &engine_config, workspace, req.workspace_bytes) != FT8_ENGINE_OK)
        goto fail;
    rx->engine_initialized = true;

    frontend_config = rx_frontend_baseline_config();
    if (rx_frontend_init(&rx->frontend, &frontend_config) != RX_FRONTEND_OK) goto fail;
    rx->frontend_initialized = true;

    if (rx_slot_framer_init(&rx->framer, slot_id, sample_offset) != RX_SLOT_FRAMER_OK) goto fail;
    rx->framer_initialized = true;

    builder_config = rx_result_builder_default_config();
    if (rx_result_builder_init(&rx->builder, &builder_config) != RX_RESULT_OK) goto fail;
    rx->builder_initialized = true;

    if (rx_audio_adapter_init(&rx->audio, app->api->audio) != RX_AUDIO_ADAPTER_OK) goto fail;
    rx->audio_initialized = true;
    if (rx_audio_adapter_open(&rx->audio, config->endpoint) != RX_AUDIO_ADAPTER_OK ||
        rx_audio_adapter_start(&rx->audio) != RX_AUDIO_ADAPTER_OK) {
        goto fail;
    }

    rx->active = true;
    return true;

fail:
    app_rx_destroy(app);
    return false;
}

bool app_controller_step_rx(AppController *app, bool *out_model_changed)
{
    AppRxState *rx;
    RxAudioAdapterStatus audio_status;
    size_t got = 0u;
    size_t out_count = 0u;
    uint64_t generation_before;

    if (out_model_changed == NULL) return false;
    *out_model_changed = false;
    if (app == NULL || app->rx == NULL) return true;

    rx = app->rx;
    if (!rx->active) return true;
    generation_before = rx->batch_generation;

    audio_status = rx_audio_adapter_read(&rx->audio, rx->transport_frames,
                                         RX_TRANSPORT_FRAMES, &got, 20u);
    if (audio_status == RX_AUDIO_ADAPTER_END_OF_STREAM) {
        if (rx_audio_adapter_close(&rx->audio) != RX_AUDIO_ADAPTER_OK) return false;
        rx->active = false;
        return true;
    }
    if (audio_status != RX_AUDIO_ADAPTER_OK) {
        mini_result_t last = rx_audio_adapter_last_result(&rx->audio);
        if (last == MINI_ERR_TIMEOUT || last == MINI_ERR_NOT_READY) return true;
        return false;
    }
    if (got == 0u) return true;

    if (rx_frontend_process(&rx->frontend, rx->transport_frames, got,
                            rx->frontend_samples, RX_FRONTEND_OUT_CAPACITY,
                            &out_count) != RX_FRONTEND_OK) {
        return false;
    }
    if (out_count > 0u &&
        rx_slot_framer_process(&rx->framer, rx->frontend_samples, out_count,
                               rx_emit_event, rx) != RX_SLOT_FRAMER_OK) {
        return false;
    }

    if (rx->batch_generation != generation_before) *out_model_changed = true;
    return true;
}

bool app_controller_rx_active(const AppController *app)
{
    return app != NULL && app->rx != NULL && app->rx->active;
}

static void build_utc_model(const AppController *app, UiModel *model)
{
    mini_utc_time_t utc = {.struct_size = sizeof(utc)};
    int64_t seconds_of_day;

    if (app == NULL || app->api == NULL || app->api->time_location == NULL ||
        (app->api->time_location->capabilities & MINI_TIMELOC_CAP_UTC) == 0u ||
        app->api->time_location->utc_get == NULL ||
        app->api->time_location->utc_get(&utc) != MINI_OK) {
        return;
    }

    seconds_of_day = utc.unix_seconds % 86400;
    if (seconds_of_day < 0) seconds_of_day += 86400;
    model->utc_valid = true;
    model->utc_hour = (uint8_t)(seconds_of_day / 3600);
    model->utc_minute = (uint8_t)((seconds_of_day / 60) % 60);
    model->utc_second = (uint8_t)(seconds_of_day % 60);
    model->slot_counter = (uint8_t)(seconds_of_day % 15);
}

void app_controller_build_ui_model(const AppController *app, UiModel *model)
{
    size_t i;
    memset(model, 0, sizeof(*model));

    model->profile_index = app->config.profile_index;
    model->profile_count = config_service_profile_count();
    snprintf(model->profile_name, sizeof(model->profile_name), "%s",
             config_service_profile_name(model->profile_index));

    model->band_index = app->config.band_index;
    model->band_count = config_service_band_count(model->profile_index);
    snprintf(model->band_name, sizeof(model->band_name), "%s",
             config_service_band_name(model->profile_index, model->band_index));

    model->skip_tx1 = qso_scheduler_get_skip_tx1(&app->scheduler);
    model->max_retry = qso_scheduler_get_max_retry(&app->scheduler);
    build_utc_model(app, model);

    if (app->rx != NULL && app->rx->have_batch) {
        size_t count = app->rx->batch.message_count;
        if (count > APP_MAX_RX_LINES) count = APP_MAX_RX_LINES;
        model->rx_count = count;
        for (i = 0u; i < count; ++i) {
            copy_ui_text(model->rx_lines[i], app->rx->batch.messages[i].canonical_text);
        }
    }

    model->tx_count = 1u;
    snprintf(model->tx_lines[0], UI_TEXT_CAP, "%s", "TX queue empty (prototype)");
}

bool app_controller_apply_action(AppController *app, const AppAction *action)
{
    if (app == NULL || action == NULL) return false;
    bool changed = false;

    switch (action->type) {
        case APP_ACTION_SET_PROFILE:
            config_service_set_profile(&app->config, action->value.index);
            changed = true;
            break;

        case APP_ACTION_SET_BAND:
            config_service_set_band(&app->config, action->value.index);
            changed = true;
            break;

        case APP_ACTION_SET_SKIP_TX1:
            qso_scheduler_set_skip_tx1(&app->scheduler, action->value.bool_value);
            config_service_set_skip_tx1(&app->config, action->value.bool_value);
            changed = true;
            break;

        case APP_ACTION_SET_MAX_RETRY:
            qso_scheduler_set_max_retry(&app->scheduler, action->value.int_value);
            config_service_set_max_retry(&app->config,
                                         qso_scheduler_get_max_retry(&app->scheduler));
            changed = true;
            break;

        case APP_ACTION_NONE:
        default:
            break;
    }

    if (!changed) return false;
    return app_save_config(app);
}

void app_controller_shutdown(AppController *app)
{
    if (app == NULL) return;
    app_rx_destroy(app);
}

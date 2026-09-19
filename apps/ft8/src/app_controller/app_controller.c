#include "app_controller.h"
#include "app_controller_tx.h"
#include "app_rx_order.h"
#include "tx_offset.h"

#include <ctype.h>
#include <limits.h>
#include <stddef.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "ft8_engine.h"
#include "rx_audio_adapter.h"
#include "rx_frontend.h"
#include "rx_result_builder.h"
#include "rx_slot_framer.h"

#define RX_TRANSPORT_FRAMES 257u
#define RX_FRONTEND_OUT_CAPACITY ((RX_TRANSPORT_FRAMES + 1u) / 2u)
#define RX_READY_DRAIN_LIMIT 8u

typedef enum {
    RX_DECODE_ASYNC_IDLE = 0,
    RX_DECODE_ASYNC_RUNNING,
    RX_DECODE_ASYNC_RESULT,
    RX_DECODE_ASYNC_CANCEL_REQUESTED,
    RX_DECODE_ASYNC_CANCELED,
    RX_DECODE_ASYNC_ERROR
} RxDecodeAsyncState;


/* Application composition may select a smaller monitor workspace. The engine's
 * portable baseline and time oversampling remain unchanged. */
#ifndef FT8_DEFAULT_FREQ_OSR
#define FT8_DEFAULT_FREQ_OSR FT8_MONITOR_BASELINE_FREQ_OSR
#endif

#ifndef FT8_DECODE_DIAGNOSTICS
#define FT8_DECODE_DIAGNOSTICS 0
#endif

_Static_assert(APP_MAX_TX_LINES >= AUTO_SEQ_MAX_QUEUE,
               "UiModel must hold the complete active AutoSeq queue");
_Static_assert(FT8_CONFIG_CQ == AUTO_SEQ_CQ, "CQ type mapping drifted");
_Static_assert(FT8_CONFIG_CQ_SOTA == AUTO_SEQ_CQ_SOTA, "CQ type mapping drifted");
_Static_assert(FT8_CONFIG_CQ_POTA == AUTO_SEQ_CQ_POTA, "CQ type mapping drifted");
_Static_assert(FT8_CONFIG_CQ_QRP == AUTO_SEQ_CQ_QRP, "CQ type mapping drifted");
_Static_assert(FT8_CONFIG_CQ_FD == AUTO_SEQ_CQ_FD, "CQ type mapping drifted");
_Static_assert(FT8_CONFIG_CQ_FREETEXT == AUTO_SEQ_CQ_FREETEXT, "CQ type mapping drifted");

struct AppRxState {
    const mini_api_t *api;
    RxAudioAdapter audio;
    RxFrontend frontend;
    RxSlotFramer framer;
    Ft8Engine engine;
    RxResultBuilder builder;

    Ft8ProtocolMessage protocol_messages[FT8_ENGINE_JOB_CANDIDATE_CAPACITY];
    RxMessage rx_messages[FT8_ENGINE_JOB_CANDIDATE_CAPACITY];
    RxBatch batch;
    size_t display_order[FT8_ENGINE_JOB_CANDIDATE_CAPACITY];
    size_t display_count;
    uint64_t display_generation;

    void *engine_allocation;
    uint32_t engine_allocation_bytes;

    int16_t transport_frames[RX_TRANSPORT_FRAMES * RX_AUDIO_ADAPTER_CHANNELS];
    float frontend_samples[RX_FRONTEND_OUT_CAPACITY];

    bool engine_initialized;
    bool frontend_initialized;
    bool framer_initialized;
    bool timing_pending;
    bool builder_initialized;
    bool audio_initialized;
    bool active;
    bool have_batch;
    bool live;
    bool have_applied_batch;
    int64_t applied_slot;
    uint64_t applied_generation;
    uint64_t rt_log_failures;
    uint64_t batch_generation;

    bool decode_external;
    atomic_int decode_async_state;
    Ft8ProtocolSlot decode_completed_slot;
    int64_t decode_diag_start_ms;
    bool decode_diag_search_reported;

    /* UI selection is an index into the retained batch, never a retained pointer. */
    bool selected_rx_valid;
    size_t selected_rx_index;
    uint64_t selected_rx_generation;
};

static RxDecodeAsyncState rx_decode_state(AppRxState *rx)
{
    return (RxDecodeAsyncState)atomic_load_explicit(
        &rx->decode_async_state, memory_order_acquire);
}

static int64_t rx_monotonic_ms(const AppRxState *rx)
{
    uint64_t us;
    uint64_t ms;

    if (rx == NULL || rx->api == NULL || rx->api->time_location == NULL ||
        rx->api->time_location->monotonic_us == NULL) {
        return 0;
    }

    us = rx->api->time_location->monotonic_us();
    ms = us / 1000u;
    return ms > (uint64_t)INT64_MAX ? INT64_MAX : (int64_t)ms;
}

static void rx_decode_diag(AppRxState *rx, const char *event,
                           int64_t slot_id, int64_t elapsed_ms,
                           size_t candidates, size_t messages,
                           int state)
{
#if FT8_DECODE_DIAGNOSTICS
    char line[128];

    if (rx == NULL || event == NULL || rx->api == NULL ||
        rx->api->system == NULL || rx->api->system->write == NULL) {
        return;
    }

    (void)snprintf(line, sizeof(line),
                   "FT8D %s slot=%lld ms=%lld cand=%u msg=%u state=%d\n",
                   event,
                   (long long)slot_id,
                   (long long)elapsed_ms,
                   (unsigned)candidates,
                   (unsigned)messages,
                   state);
    rx->api->system->write(line);
#else
    (void)rx;
    (void)event;
    (void)slot_id;
    (void)elapsed_ms;
    (void)candidates;
    (void)messages;
    (void)state;
#endif
}

static void rx_request_decode_cancel(AppRxState *rx)
{
    int expected;

    if (rx == NULL || !rx->decode_external)
        return;

    expected = RX_DECODE_ASYNC_RUNNING;
    (void)atomic_compare_exchange_strong_explicit(
        &rx->decode_async_state, &expected, RX_DECODE_ASYNC_CANCEL_REQUESTED,
        memory_order_acq_rel, memory_order_acquire);
}

static int rx_decode_blocks_stream_reset(AppRxState *rx)
{
    RxDecodeAsyncState state;

    if (rx == NULL || !rx->decode_external)
        return 0;

    state = rx_decode_state(rx);
    return state == RX_DECODE_ASYNC_RUNNING ||
           state == RX_DECODE_ASYNC_CANCEL_REQUESTED;
}

static void rx_invalidate_order(AppRxState *rx)
{
    rx->display_count = 0;
    rx->selected_rx_valid = false;
}

static void rx_complete_batch(AppRxState *rx)
{
    rx->have_batch = true;
    rx->selected_rx_valid = false;
    ++rx->batch_generation;
    rx->display_count = app_rx_order_build(rx->batch.messages, rx->batch.message_count,
                                           rx->display_order, FT8_ENGINE_JOB_CANDIDATE_CAPACITY);
    rx->display_generation = rx->batch_generation;
}

static void copy_ui_text(char out[UI_TEXT_CAP], const char *text)
{
    size_t length = strlen(text);
    if (length >= UI_TEXT_CAP) length = UI_TEXT_CAP - 1u;
    memcpy(out, text, length);
    out[length] = '\0';
}

static bool looks_like_grid4(const char *text)
{
    return text != NULL && strlen(text) == 4u &&
           isalpha((unsigned char)text[0]) &&
           isalpha((unsigned char)text[1]) &&
           isdigit((unsigned char)text[2]) &&
           isdigit((unsigned char)text[3]);
}

/*
 * AS-3/AS-6 boundary: turn one retained factual CQ into the normalized event
 * consumed by pure AutoSeq. No policy is inferred from display text.
 */
static bool selected_cq_to_event(const RxBatch *batch, const RxMessage *message,
                                 AutoSeqRxEvent *out_event)
{
    int written;

    if (batch == NULL || message == NULL || out_event == NULL ||
        !message->is_cq || message->has_unresolved_hash ||
        message->call_de[0] == '\0') {
        return false;
    }

    memset(out_event, 0, sizeof(*out_event));
    out_event->rx_slot_id = batch->slot_id;
    out_event->offset_hz = message->offset_hz;
    out_event->snr_db = message->snr_db;
    out_event->report_db = AUTO_SEQ_SNR_UNKNOWN;
    out_event->kind = AUTO_SEQ_MSG_TX1;
    out_event->flags = AUTO_SEQ_RX_FLAG_CQ;
    if (message->is_fd) out_event->flags |= AUTO_SEQ_RX_FLAG_FD;

    written = snprintf(out_event->dxcall, sizeof(out_event->dxcall), "%s",
                       message->call_de);
    if (written < 0 || (size_t)written >= sizeof(out_event->dxcall)) return false;

    if (looks_like_grid4(message->extra)) {
        written = snprintf(out_event->dxgrid, sizeof(out_event->dxgrid), "%s",
                           message->extra);
        if (written < 0 || (size_t)written >= sizeof(out_event->dxgrid)) return false;
    }
    return true;
}

static AutoSeqMessageKind auto_seq_kind_from_rx(RxQsoMessageKind kind)
{
    switch (kind) {
        case RX_QSO_MSG_TX1: return AUTO_SEQ_MSG_TX1;
        case RX_QSO_MSG_TX2: return AUTO_SEQ_MSG_TX2;
        case RX_QSO_MSG_TX3: return AUTO_SEQ_MSG_TX3;
        case RX_QSO_MSG_TX4: return AUTO_SEQ_MSG_TX4;
        case RX_QSO_MSG_TX5: return AUTO_SEQ_MSG_TX5;
        default: return AUTO_SEQ_MSG_NONE;
    }
}

/*
 * AS-4/AS-6 boundary: map factual addressed RX metadata into AutoSeq's
 * normalized event. Structured Field Day facts are copied directly from the
 * RxMessage; DXpedition remains deferred to its own later work.
 */
static bool addressed_rx_to_event(const RxBatch *batch, const RxMessage *message,
                                  AutoSeqRxEvent *out_event)
{
    AutoSeqMessageKind kind;
    int written;

    if (batch == NULL || message == NULL || out_event == NULL ||
        !message->is_to_me || message->parse_status != FT8_PROTOCOL_PARSE_OK ||
        message->has_unresolved_hash || message->call_de[0] == '\0') {
        return false;
    }

    kind = auto_seq_kind_from_rx(message->qso_kind);
    if (kind == AUTO_SEQ_MSG_NONE) return false;

    memset(out_event, 0, sizeof(*out_event));
    out_event->rx_slot_id = batch->slot_id;
    out_event->offset_hz = message->offset_hz;
    out_event->snr_db = message->snr_db;
    out_event->report_db = message->report_db;
    out_event->kind = kind;
    out_event->flags = AUTO_SEQ_RX_FLAG_TO_ME;
    if (message->is_fd) out_event->flags |= AUTO_SEQ_RX_FLAG_FD;

    written = snprintf(out_event->dxcall, sizeof(out_event->dxcall), "%s",
                       message->call_de);
    if (written < 0 || (size_t)written >= sizeof(out_event->dxcall)) return false;

    if (kind == AUTO_SEQ_MSG_TX1 && looks_like_grid4(message->extra)) {
        written = snprintf(out_event->dxgrid, sizeof(out_event->dxgrid), "%s",
                           message->extra);
        if (written < 0 || (size_t)written >= sizeof(out_event->dxgrid)) return false;
    }
    if (message->is_fd && message->fd_exchange[0] != '\0') {
        written = snprintf(out_event->fd_exchange, sizeof(out_event->fd_exchange), "%s",
                           message->fd_exchange);
        if (written < 0 || (size_t)written >= sizeof(out_event->fd_exchange)) return false;
    }
    return true;
}

static const char *qso_state_label(AutoSeqState state)
{
    switch (state) {
        case AUTO_SEQ_STATE_CALLING: return "CALL";
        case AUTO_SEQ_STATE_REPLYING: return "RPLY";
        case AUTO_SEQ_STATE_REPORT: return "RPRT";
        case AUTO_SEQ_STATE_ROGER_REPORT: return "RRPT";
        case AUTO_SEQ_STATE_ROGERS: return "RGRS";
        case AUTO_SEQ_STATE_SIGNOFF: return "SOFF";
        default: return "----";
    }
}

static int64_t app_monotonic_ms(const AppController *app)
{
    uint64_t us;
    uint64_t ms;

    if (app == NULL || app->api == NULL || app->api->time_location == NULL ||
        app->api->time_location->monotonic_us == NULL) {
        return 0;
    }

    us = app->api->time_location->monotonic_us();
    ms = us / 1000u;
    return ms > (uint64_t)INT64_MAX ? INT64_MAX : (int64_t)ms;
}

static bool app_save_config(AppController *app)
{
    char text[2048];
    if (!config_service_serialize(&app->config, text, sizeof(text))) return false;
    return storage_service_write_text_atomic(&app->storage, app->station_path, text);
}

static bool app_sync_auto_seq_config(AppController *app)
{
    if (app == NULL) return false;
    if (!auto_seq_set_station(&app->auto_seq, app->config.callsign, app->effective_grid))
        return false;

    auto_seq_set_skip_tx1(&app->auto_seq, app->config.skip_tx1);
    auto_seq_set_max_retry(&app->auto_seq, app->config.max_retry);

    if (!auto_seq_set_cq(&app->auto_seq,
                         (AutoSeqCqType)app->config.cq_type,
                         app->config.cq_freetext)) {
        return false;
    }
    return auto_seq_set_fd_exchange(&app->auto_seq, app->config.fd_exchange);
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

static bool backdate_slot_reference(int64_t *slot_id,
                                    uint32_t *sample_offset,
                                    size_t sample_count)
{
    if (slot_id == NULL || sample_offset == NULL ||
        *sample_offset >= RX_SLOT_FRAMER_SLOT_SAMPLES) {
        return false;
    }

    while (sample_count > 0u) {
        if (sample_count <= (size_t)*sample_offset) {
            *sample_offset -= (uint32_t)sample_count;
            return true;
        }

        sample_count -= (size_t)*sample_offset;
        if (*slot_id == INT64_MIN) return false;
        --(*slot_id);
        *sample_offset = RX_SLOT_FRAMER_SLOT_SAMPLES;
    }
    return true;
}

static int rx_emit_event(void *ctx, const RxSlotFramerEvent *event)
{
    AppRxState *rx = (AppRxState *)ctx;
    Ft8EngineStatus status;

    if (rx == NULL || event == NULL) return -1;

    switch (event->type) {
    case RX_SLOT_FRAMER_EVENT_BEGIN_WINDOW:
        return ft8_engine_begin_window(&rx->engine, event->slot_id) == FT8_ENGINE_OK ? 0 : -1;

    case RX_SLOT_FRAMER_EVENT_ENGINE_BLOCK:
        return ft8_engine_process_block(&rx->engine, event->samples) == FT8_ENGINE_OK ? 0 : -1;

    case RX_SLOT_FRAMER_EVENT_FINALIZE_WINDOW:
        if (rx->decode_external &&
            rx_decode_state(rx) != RX_DECODE_ASYNC_IDLE) {
            RxDecodeAsyncState state = rx_decode_state(rx);
            int64_t now_ms = rx_monotonic_ms(rx);
            int64_t elapsed = rx->decode_diag_start_ms > 0
                                  ? now_ms - rx->decode_diag_start_ms
                                  : 0;
            rx_decode_diag(rx, "skip-busy", event->slot_id, elapsed,
                           0u, 0u, (int)state);
            /* One worker job/result at a time. A slow previous slot costs
             * decode yield, never capture continuity. */
            return 0;
        }
        status = ft8_engine_start_decode(&rx->engine,
                                         rx->protocol_messages,
                                         FT8_ENGINE_JOB_CANDIDATE_CAPACITY);
        if (status == FT8_ENGINE_OK) {
            rx->decode_diag_start_ms = rx_monotonic_ms(rx);
            rx->decode_diag_search_reported = false;
            rx_decode_diag(rx, "start", event->slot_id, 0,
                           0u, 0u,
                           rx->decode_external ? RX_DECODE_ASYNC_RUNNING
                                               : RX_DECODE_ASYNC_IDLE);
        }
        if (status == FT8_ENGINE_OK && rx->decode_external) {
            atomic_store_explicit(&rx->decode_async_state,
                                  RX_DECODE_ASYNC_RUNNING,
                                  memory_order_release);
        }
        return (status == FT8_ENGINE_OK || status == FT8_ENGINE_BUSY) ? 0 : -1;

    case RX_SLOT_FRAMER_EVENT_STREAM_RESET:
        if (rx_decode_blocks_stream_reset(rx))
            return -1;
        return ft8_engine_reset_stream(&rx->engine) == FT8_ENGINE_OK ? 0 : -1;
    }

    return -1;
}

static bool app_process_addressed_batch(AppController *app)
{
    size_t i;

    if (app == NULL || app->rx == NULL || !app->rx->have_batch) return true;

    if (app->rx->have_applied_batch && app->rx->applied_generation == app->rx->batch_generation) return true;
    bool log_failed = false;
    for (i = 0u; i < app->rx->batch.message_count; ++i) {
        const RxMessage *message = &app->rx->batch.messages[i];
        AutoSeqRxEvent event;
        AutoSeqResult result;

        if (app->config.rxtx_log && !log_service_write_rt(&app->log, false, app->config.band_index,
                                                         message->canonical_text, message->snr_db, message->offset_hz)) {
            ++app->rx->rt_log_failures;
            log_failed = true;
        }
        if (!addressed_rx_to_event(&app->rx->batch, message, &event)) continue;

        result = auto_seq_on_addressed_rx(&app->auto_seq, &event);
        if (result == AUTO_SEQ_ERR_INVALID) return false;
        /* V2 silently drops a new decode when all 30 entries are active. */
    }
    app->rx->have_applied_batch = true;
    app->rx->applied_generation = app->rx->batch_generation;
    app->rx->applied_slot = app->rx->batch.slot_id;
    if (log_failed && app->api->system && app->api->system->write)
        app->api->system->write("ft8: RX RT log failed; receive processing continues\n");
    return true;
}

static bool app_publish_external_decode(AppController *app)
{
    AppRxState *rx;
    RxDecodeAsyncState state;
    RxResultStatus result_status;

    if (app == NULL || app->rx == NULL)
        return true;
    rx = app->rx;
    if (!rx->decode_external)
        return true;

    state = rx_decode_state(rx);
    if (state == RX_DECODE_ASYNC_ERROR)
        return false;
    if (state == RX_DECODE_ASYNC_CANCELED) {
        atomic_store_explicit(&rx->decode_async_state,
                              RX_DECODE_ASYNC_IDLE,
                              memory_order_release);
        return true;
    }
    if (state != RX_DECODE_ASYNC_RESULT)
        return true;

    result_status = rx_result_builder_build(&rx->builder,
                                            &rx->decode_completed_slot,
                                            rx->rx_messages,
                                            FT8_ENGINE_JOB_CANDIDATE_CAPACITY,
                                            &rx->batch);
    if (result_status != RX_RESULT_OK)
        return false;

    rx_complete_batch(rx);
    rx_decode_diag(rx, "publish", rx->batch.slot_id,
                   rx_monotonic_ms(rx) - rx->decode_diag_start_ms,
                   rx->engine.decode_candidate_count,
                   rx->batch.message_count,
                   RX_DECODE_ASYNC_IDLE);
    atomic_store_explicit(&rx->decode_async_state,
                          RX_DECODE_ASYNC_IDLE,
                          memory_order_release);
    return true;
}

static bool app_service_decode(AppController *app)
{
    AppRxState *rx;
    Ft8ProtocolSlot slot;
    Ft8EngineStatus engine_status;
    RxResultStatus result_status;
    int completed = 0;

    if (app == NULL || app->rx == NULL)
        return true;
    rx = app->rx;

    /* A discontinuity invalidates the waterfall/timing reference. Wait for the
     * framer reset on fresh data instead of spending CPU on a stale job. */
    if (rx->timing_pending || !ft8_engine_decode_active(&rx->engine))
        return true;

    engine_status = ft8_engine_decode_step(&rx->engine, &completed, &slot);
    if (engine_status != FT8_ENGINE_OK &&
        engine_status != FT8_ENGINE_NO_MESSAGES) {
        return false;
    }
    if (!completed)
        return true;

    result_status = rx_result_builder_build(&rx->builder, &slot,
                                            rx->rx_messages,
                                            FT8_ENGINE_JOB_CANDIDATE_CAPACITY,
                                            &rx->batch);
    if (result_status != RX_RESULT_OK)
        return false;

    rx_complete_batch(rx);
    return true;
}

static bool app_finish_rx_step(AppController *app,
                               uint64_t generation_before,
                               bool *out_model_changed)
{
    if (app->rx != NULL && app->rx->decode_external) {
        if (!app_publish_external_decode(app))
            return false;
    } else if (!app_service_decode(app)) {
        return false;
    }

    if (app->rx != NULL && app->rx->batch_generation != generation_before) {
        if (!app_process_addressed_batch(app))
            return false;
        *out_model_changed = true;
    }
    return true;
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
    const mini_time_location_api_t *time = api->time_location;
    uint64_t monotonic = time && time->monotonic_us ? time->monotonic_us() : 0;
    mini_utc_time_t utc = {.struct_size = sizeof(utc)};
    if (!time || !(time->capabilities & MINI_TIMELOC_CAP_UTC) || !time->utc_get ||
        time->utc_get(&utc) != MINI_OK || utc.nanoseconds >= 1000000000u) {
        utc.unix_seconds = 0;
        utc.nanoseconds = 0;
    }
    app->tx.offset_rng = tx_offset_seed(monotonic, utc.unix_seconds, utc.nanoseconds);

    if (!storage_service_init(&app->storage, api->fs)) return false;
    if (!storage_service_ensure_directory(&app->storage, data_directory)) return false;

    int path_length = snprintf(app->station_path, sizeof(app->station_path), "%s", station_path);
    if (path_length < 0 || (size_t)path_length >= sizeof(app->station_path)) return false;

    if (!log_service_init(&app->log, api->fs, api->time_location, app->station_path))
        return false;

    config_service_defaults(&app->config);

    char text[2048];
    StorageReadResult loaded = storage_service_read_text(&app->storage, app->station_path,
                                                         text, sizeof(text));
    switch (loaded) {
    case STORAGE_READ_FOUND:
        if (!config_service_parse(&app->config, text)) return false;
        break;
    case STORAGE_READ_NOT_FOUND:
        break;
    case STORAGE_READ_ERROR:
    default:
        return false;
    }

    (void)snprintf(app->effective_grid, sizeof(app->effective_grid), "%s", app->config.grid);

    if (!auto_seq_init(&app->auto_seq, NULL) || !app_sync_auto_seq_config(app))
        return false;

    if (loaded == STORAGE_READ_NOT_FOUND && !app_save_config(app)) return false;
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
    int64_t slot_id = 0;
    uint32_t sample_offset = 0u;

    if (app == NULL || app->api == NULL || config == NULL || config->endpoint == NULL ||
        app->rx != NULL || app->api->memory == NULL || app->api->memory->alloc == NULL ||
        app->api->memory->free == NULL || app->api->audio == NULL) {
        return false;
    }

    if (config->has_explicit_timing) {
        slot_id = config->slot_id;
        sample_offset = config->sample_offset;
        if (sample_offset >= RX_SLOT_FRAMER_SLOT_SAMPLES) return false;
    }

    if (sizeof(*rx) > UINT32_MAX ||
        app->api->memory->alloc((uint32_t)sizeof(*rx), (void **)&rx) != MINI_OK || rx == NULL) {
        return false;
    }
    memset(rx, 0, sizeof(*rx));
    rx->api = app->api;
    atomic_init(&rx->decode_async_state, RX_DECODE_ASYNC_IDLE);
    app->rx = rx;
    rx->live = !config->has_explicit_timing;

    engine_config = ft8_engine_baseline_config();
    engine_config.monitor.freq_osr = FT8_DEFAULT_FREQ_OSR;
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

    if (config->has_explicit_timing) {
        if (rx_slot_framer_init(&rx->framer, slot_id, sample_offset) != RX_SLOT_FRAMER_OK) goto fail;
        rx->framer_initialized = true;
        rx->timing_pending = false;
    } else {
        rx->timing_pending = true;
    }

    builder_config = rx_result_builder_default_config();
    (void)snprintf(builder_config.local_callsign,
                   sizeof(builder_config.local_callsign), "%s", app->config.callsign);
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

static bool app_process_rx_frames(AppController *app, size_t got)
{
    AppRxState *rx;
    size_t out_count = 0u;

    if (app == NULL || app->rx == NULL)
        return false;
    rx = app->rx;

    if (got == 0u)
        return true;

    if (rx->timing_pending && rx_decode_blocks_stream_reset(rx))
        return true;

    if (rx->decode_external &&
        rx_decode_state(rx) == RX_DECODE_ASYNC_CANCELED) {
        atomic_store_explicit(&rx->decode_async_state,
                              RX_DECODE_ASYNC_IDLE,
                              memory_order_release);
    }

    if (rx_frontend_process(&rx->frontend, rx->transport_frames, got,
                            rx->frontend_samples, RX_FRONTEND_OUT_CAPACITY,
                            &out_count) != RX_FRONTEND_OK) {
        return false;
    }

    if (rx->timing_pending && out_count > 0u) {
        int64_t first_slot_id;
        uint32_t first_sample_offset;

        if (!utc_to_slot_reference(app->api->time_location,
                                   &first_slot_id,
                                   &first_sample_offset) ||
            !backdate_slot_reference(&first_slot_id,
                                     &first_sample_offset,
                                     out_count)) {
            return false;
        }

        RxSlotFramerStatus status = rx->framer_initialized
            ? rx_slot_framer_reset_stream(&rx->framer, first_slot_id,
                                           first_sample_offset, rx_emit_event, rx)
            : rx_slot_framer_init(&rx->framer, first_slot_id, first_sample_offset);
        if (status != RX_SLOT_FRAMER_OK)
            return false;
        rx->framer_initialized = true;
        rx->timing_pending = false;
    }

    if (out_count > 0u &&
        (!rx->framer_initialized ||
         rx_slot_framer_process(&rx->framer, rx->frontend_samples, out_count,
                                rx_emit_event, rx) != RX_SLOT_FRAMER_OK)) {
        return false;
    }

    return true;
}

bool app_controller_step_rx(AppController *app, bool *out_model_changed)
{
    AppRxState *rx;
    RxAudioAdapterStatus audio_status;
    size_t got = 0u;
    uint64_t generation_before;

    if (out_model_changed == NULL) return false;
    *out_model_changed = false;
    if (app == NULL || app->rx == NULL) return true;

    rx = app->rx;
    if (!rx->active) return true;
    generation_before = rx->batch_generation;

    audio_status = rx_audio_adapter_read(&rx->audio, rx->transport_frames,
                                         RX_TRANSPORT_FRAMES, &got, 20u);
    if (audio_status == RX_AUDIO_ADAPTER_DISCONTINUITY) {
        rx_frontend_reset_stream(&rx->frontend);
        rx->timing_pending = true;
        rx_request_decode_cancel(rx);
        return app_finish_rx_step(app, generation_before, out_model_changed);
    }
    if (audio_status == RX_AUDIO_ADAPTER_END_OF_STREAM) {
        if (rx_audio_adapter_close(&rx->audio) != RX_AUDIO_ADAPTER_OK) return false;
        rx->active = false;
        return app_finish_rx_step(app, generation_before, out_model_changed);
    }
    if (audio_status != RX_AUDIO_ADAPTER_OK) {
        mini_result_t last = rx_audio_adapter_last_result(&rx->audio);
        if (last == MINI_ERR_TIMEOUT || last == MINI_ERR_NOT_READY)
            return app_finish_rx_step(app, generation_before, out_model_changed);
        return false;
    }

    if (!app_process_rx_frames(app, got))
        return false;

    /*
     * I001 capture-before-decode policy.
     *
     * A live provider may have accumulated audio while decode or UI work ran.
     * Drain immediately available chunks with zero wait. Portable inline decode
     * gets one bounded service unit after this drain; ADV's external decode
     * worker runs independently on the other core.
     */
    if (rx->live) {
        for (unsigned drain = 0u; drain < RX_READY_DRAIN_LIMIT; ++drain) {
            got = 0u;
            audio_status = rx_audio_adapter_read(&rx->audio,
                                                 rx->transport_frames,
                                                 RX_TRANSPORT_FRAMES,
                                                 &got,
                                                 MINI_WAIT_NONE);
            if (audio_status == RX_AUDIO_ADAPTER_DISCONTINUITY) {
                rx_frontend_reset_stream(&rx->frontend);
                rx->timing_pending = true;
                rx_request_decode_cancel(rx);
                return app_finish_rx_step(app, generation_before, out_model_changed);
            }
            if (audio_status == RX_AUDIO_ADAPTER_END_OF_STREAM) {
                if (rx_audio_adapter_close(&rx->audio) != RX_AUDIO_ADAPTER_OK)
                    return false;
                rx->active = false;
                break;
            }
            if (audio_status != RX_AUDIO_ADAPTER_OK) {
                mini_result_t last = rx_audio_adapter_last_result(&rx->audio);
                if (last == MINI_ERR_TIMEOUT || last == MINI_ERR_NOT_READY)
                    break;
                return false;
            }
            if (got == 0u)
                break;
            if (!app_process_rx_frames(app, got))
                return false;
        }
    }

    return app_finish_rx_step(app, generation_before, out_model_changed);
}

bool app_controller_rx_active(const AppController *app)
{
    return app != NULL && app->rx != NULL && app->rx->active;
}

bool app_controller_enable_decode_worker(AppController *app, bool enabled)
{
    AppRxState *rx;
    RxDecodeAsyncState state;

    if (app == NULL || app->rx == NULL)
        return false;
    rx = app->rx;
    state = rx_decode_state(rx);

    if (enabled) {
        if (rx->decode_external)
            return true;
        if (state != RX_DECODE_ASYNC_IDLE ||
            ft8_engine_decode_active(&rx->engine)) {
            return false;
        }
        rx->decode_external = true;
        return true;
    }

    if (!rx->decode_external)
        return true;
    if (state != RX_DECODE_ASYNC_IDLE)
        return false;
    rx->decode_external = false;
    return true;
}

bool app_controller_decode_worker_step(AppController *app, bool *out_did_work)
{
    AppRxState *rx;
    RxDecodeAsyncState state;
    Ft8ProtocolSlot slot;
    Ft8EngineStatus status;
    int completed = 0;

    if (out_did_work == NULL)
        return false;
    *out_did_work = false;
    if (app == NULL || app->rx == NULL || !app->rx->decode_external)
        return false;
    rx = app->rx;
    state = rx_decode_state(rx);

    if (state == RX_DECODE_ASYNC_ERROR)
        return false;

    if (state == RX_DECODE_ASYNC_CANCEL_REQUESTED) {
        *out_did_work = true;
        if (ft8_engine_cancel_decode(&rx->engine) != FT8_ENGINE_OK) {
            atomic_store_explicit(&rx->decode_async_state,
                                  RX_DECODE_ASYNC_ERROR,
                                  memory_order_release);
            return false;
        }
        rx_decode_diag(rx, "cancel", rx->engine.decode_slot_id,
                       rx_monotonic_ms(rx) - rx->decode_diag_start_ms,
                       rx->engine.decode_candidate_count,
                       rx->engine.decode_slot.message_count,
                       RX_DECODE_ASYNC_CANCELED);
        atomic_store_explicit(&rx->decode_async_state,
                              RX_DECODE_ASYNC_CANCELED,
                              memory_order_release);
        return true;
    }

    if (state != RX_DECODE_ASYNC_RUNNING)
        return true;

    *out_did_work = true;
    {
        int search_was_active = rx->engine.decode_search_active;
        status = ft8_engine_decode_step(&rx->engine, &completed, &slot);

        if (status != FT8_ENGINE_OK && status != FT8_ENGINE_NO_MESSAGES) {
            atomic_store_explicit(&rx->decode_async_state,
                                  RX_DECODE_ASYNC_ERROR,
                                  memory_order_release);
            return false;
        }

        if (search_was_active && !rx->engine.decode_search_active &&
            !rx->decode_diag_search_reported) {
            int64_t elapsed = rx_monotonic_ms(rx) - rx->decode_diag_start_ms;
            rx->decode_diag_search_reported = true;
            rx_decode_diag(rx, "search-done", rx->engine.decode_slot_id,
                           elapsed, rx->engine.decode_candidate_count,
                           rx->engine.decode_slot.message_count,
                           RX_DECODE_ASYNC_RUNNING);
        }
    }
    if (!completed)
        return true;

    rx->decode_completed_slot = slot;
    rx_decode_diag(rx, "done", slot.slot_id,
                   rx_monotonic_ms(rx) - rx->decode_diag_start_ms,
                   rx->engine.decode_candidate_count,
                   slot.message_count,
                   RX_DECODE_ASYNC_RESULT);
    atomic_store_explicit(&rx->decode_async_state,
                          RX_DECODE_ASYNC_RESULT,
                          memory_order_release);
    return true;
}

void app_controller_decode_worker_abort(AppController *app)
{
    AppRxState *rx;
    RxDecodeAsyncState state;

    if (app == NULL || app->rx == NULL || !app->rx->decode_external)
        return;
    rx = app->rx;
    state = rx_decode_state(rx);

    if (state == RX_DECODE_ASYNC_RUNNING ||
        state == RX_DECODE_ASYNC_CANCEL_REQUESTED) {
        (void)ft8_engine_cancel_decode(&rx->engine);
    }
    memset(&rx->decode_completed_slot, 0, sizeof(rx->decode_completed_slot));
    atomic_store_explicit(&rx->decode_async_state,
                          RX_DECODE_ASYNC_IDLE,
                          memory_order_release);
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
    AutoSeqQsoView qso_views[AUTO_SEQ_MAX_QUEUE];
    size_t i;
    size_t qso_count;

    memset(model, 0, sizeof(*model));

    model->profile_index = app->config.profile_index;
    model->profile_count = config_service_profile_count();
    snprintf(model->profile_name, sizeof(model->profile_name), "%s",
             config_service_profile_name(model->profile_index));

    model->band_index = app->config.band_index;
    model->band_count = config_service_band_count(model->profile_index);
    snprintf(model->band_name, sizeof(model->band_name), "%s",
             config_service_band_name(model->profile_index, model->band_index));

    model->cq_type = app->config.cq_type == FT8_CONFIG_CQ ? UI_CQ :
                     app->config.cq_type == FT8_CONFIG_CQ_POTA ? UI_CQ_POTA : UI_CQ_UNAVAILABLE;
    switch (app_controller_get_beacon_mode(app)) {
        case TX_BEACON_EVEN: model->beacon_mode = UI_BEACON_EVEN; break;
        case TX_BEACON_ODD: model->beacon_mode = UI_BEACON_ODD; break;
        default: model->beacon_mode = UI_BEACON_OFF; break;
    }
    model->skip_tx1 = auto_seq_get_skip_tx1(&app->auto_seq);
    model->max_retry = auto_seq_get_max_retry(&app->auto_seq);
    model->rx_active = app_controller_rx_active(app);
    build_utc_model(app, model);

    if (app->rx != NULL && app->rx->have_batch &&
        app->rx->display_generation == app->rx->batch_generation) {
        size_t count = app->rx->display_count;
        if (count > APP_MAX_RX_LINES) count = APP_MAX_RX_LINES;
        model->rx_count = count;
        for (i = 0u; i < count; ++i) {
            copy_ui_text(model->rx_lines[i], app->rx->batch.messages[app->rx->display_order[i]].canonical_text);
        }
    }

    qso_count = auto_seq_snapshot_active(&app->auto_seq, qso_views,
                                         AUTO_SEQ_MAX_QUEUE);
    if (qso_count > APP_MAX_TX_LINES) qso_count = APP_MAX_TX_LINES;
    model->tx_count = qso_count;
    for (i = 0u; i < qso_count; ++i) {
        (void)snprintf(model->tx_lines[i], UI_TEXT_CAP, "%-8.8s %.4s %u/%u",
                       qso_views[i].dxcall,
                       qso_state_label(qso_views[i].state),
                       (unsigned)qso_views[i].retry_counter,
                       (unsigned)qso_views[i].retry_limit);
    }
}

void app_controller_build_memory_model(const AppController *app, UiModel *model)
{
    mini_memory_info_t info = {.struct_size = sizeof(info)};

    if (model == NULL) return;
    model->memory_app_valid = false;
    model->memory_app_allocated_bytes = 0u;
    model->memory_app_allocation_count = 0u;
    model->memory_free_valid = false;
    model->memory_free_bytes = 0u;
    model->memory_largest_valid = false;
    model->memory_largest_free_block = 0u;
    model->rx_active = app_controller_rx_active(app);

    if (app == NULL || app->api == NULL || app->api->memory == NULL ||
        app->api->memory->get_info == NULL ||
        app->api->memory->get_info(&info) != MINI_OK) {
        return;
    }

    if ((info.valid_fields & MINI_MEM_INFO_APP_USAGE) != 0u) {
        model->memory_app_valid = true;
        model->memory_app_allocated_bytes = info.app_allocated_bytes;
        model->memory_app_allocation_count = info.app_allocation_count;
    }
    if ((info.valid_fields & MINI_MEM_INFO_FREE_BYTES) != 0u) {
        model->memory_free_valid = true;
        model->memory_free_bytes = info.free_bytes;
    }
    if ((info.valid_fields & MINI_MEM_INFO_LARGEST_BLOCK) != 0u) {
        model->memory_largest_valid = true;
        model->memory_largest_free_block = info.largest_free_block;
    }
}

bool app_controller_apply_action(AppController *app, const AppAction *action)
{
    bool config_changed = false;

    if (app == NULL || action == NULL) return false;
    if (app->tx.active) return true; /* Freeze queue/station facts until completion; quit remains available. */

    switch (action->type) {
        case APP_ACTION_SELECT_RX_MESSAGE: {
            const RxMessage *message;
            AutoSeqRxEvent event;
            AutoSeqResult auto_seq_result;

            if (app->rx == NULL || !app->rx->have_batch || action->value.index < 0 ||
                app->rx->display_generation != app->rx->batch_generation ||
                (size_t)action->value.index >= app->rx->display_count ||
                (size_t)action->value.index >= APP_MAX_RX_LINES) {
                return false;
            }
            app->rx->selected_rx_index = app->rx->display_order[(size_t)action->value.index];
            app->rx->selected_rx_generation = app->rx->batch_generation;
            app->rx->selected_rx_valid = true;

            message = &app->rx->batch.messages[app->rx->selected_rx_index];
            if (!selected_cq_to_event(&app->rx->batch, message, &event)) {
                return true;
            }

            auto_seq_result = auto_seq_on_manual_rx(&app->auto_seq, &event);
            return auto_seq_result == AUTO_SEQ_OK || auto_seq_result == AUTO_SEQ_IGNORED;
        }

        case APP_ACTION_DROP_TX_QSO:
            if (action->value.index >= 0 &&
                (size_t)action->value.index < auto_seq_active_count(&app->auto_seq)) {
                (void)auto_seq_drop_index(&app->auto_seq,
                                          (size_t)action->value.index,
                                          app_monotonic_ms(app));
            }
            return true;

        case APP_ACTION_ROTATE_TX_QUEUE:
            (void)auto_seq_rotate_same_parity(&app->auto_seq);
            return true;

        case APP_ACTION_SET_CQ_TYPE: {
            int value = action->value.int_value;
            if (value != UI_CQ && value != UI_CQ_POTA) return false;
            Ft8ConfigCqType previous = app->config.cq_type;
            if (!config_service_set_cq_type(&app->config,
                    value == UI_CQ ? FT8_CONFIG_CQ : FT8_CONFIG_CQ_POTA)) return false;
            if (app_sync_auto_seq_config(app) && app_save_config(app)) return true;
            /* A failed atomic save must not leave the model advertising a commit. */
            app->config.cq_type = previous;
            (void)app_sync_auto_seq_config(app);
            return false;
        }

        case APP_ACTION_SET_BEACON_MODE:
            switch (action->value.int_value) {
                case UI_BEACON_OFF: return app_controller_set_beacon_mode(app, TX_BEACON_OFF);
                case UI_BEACON_EVEN: return app_controller_set_beacon_mode(app, TX_BEACON_EVEN);
                case UI_BEACON_ODD: return app_controller_set_beacon_mode(app, TX_BEACON_ODD);
                default: return false;
            }

        case APP_ACTION_SET_PROFILE:
            config_service_set_profile(&app->config, action->value.index);
            config_changed = true;
            break;

        case APP_ACTION_SET_BAND:
            config_service_set_band(&app->config, action->value.index);
            config_changed = true;
            break;

        case APP_ACTION_SET_SKIP_TX1:
            auto_seq_set_skip_tx1(&app->auto_seq, action->value.bool_value);
            config_service_set_skip_tx1(&app->config, action->value.bool_value);
            config_changed = true;
            break;

        case APP_ACTION_SET_MAX_RETRY:
            auto_seq_set_max_retry(&app->auto_seq, action->value.int_value);
            config_service_set_max_retry(&app->config,
                                         auto_seq_get_max_retry(&app->auto_seq));
            config_changed = true;
            break;

        case APP_ACTION_NONE:
        default:
            return false;
    }

    return config_changed && app_save_config(app);
}

void app_controller_shutdown(AppController *app)
{
    if (app == NULL) return;
    (void)radio_control_close(&app->radio);
    app->tx.active = app->tx.pending = false;
    app_rx_destroy(app);
    app->tx.rx_paused = false;
}

mini_result_t app_controller_start_cat(AppController *app, const char *endpoint)
{
    if (!app) return MINI_ERR_INVALID;
    return radio_control_open_qmx(&app->radio, app->api, endpoint,
                                  config_service_band_dial_hz(app->config.band_index));
}

bool app_controller_pause_rx_for_tx(AppController *app)
{
    if (!app->rx || !app->rx->active) return true;
    if (rx_audio_adapter_stop(&app->rx->audio) != RX_AUDIO_ADAPTER_OK) return false;
    app->tx.rx_paused = true;
    app->rx->active = false;
    rx_frontend_reset_stream(&app->rx->frontend);
    app->rx->timing_pending = true;
    rx_request_decode_cancel(app->rx);
    return true;
}

bool app_controller_resume_rx_after_tx(AppController *app)
{
    if (!app->tx.rx_paused) return true;
    if (!app->rx || rx_audio_adapter_start(&app->rx->audio) != RX_AUDIO_ADAPTER_OK) return false;
    rx_invalidate_order(app->rx);
    rx_frontend_reset_stream(&app->rx->frontend);
    app->rx->timing_pending = true;
    app->rx->active = true;
    app->tx.rx_paused = false;
    return true;
}

bool app_controller_rx_ready_for_tx(const AppController *app, int64_t slot_id)
{
    if (!app->rx || !app->rx->active || !app->rx->live) return true;
    return slot_id != INT64_MIN && app->rx->have_applied_batch && app->rx->applied_slot == slot_id - 1;
}

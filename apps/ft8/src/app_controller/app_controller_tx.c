#include "app_controller_tx.h"

#include <limits.h>
#include <string.h>

static int64_t clamp_monotonic_ms(uint64_t monotonic_us)
{
    uint64_t ms = monotonic_us / 1000u;
    return ms > (uint64_t)INT64_MAX ? INT64_MAX : (int64_t)ms;
}

static bool utc_slot_position(const mini_time_location_api_t *time_location,
                              int64_t *out_slot_id,
                              uint16_t *out_ms_into_slot)
{
    mini_utc_time_t utc = {.struct_size = sizeof(utc)};
    int64_t slot_id;
    int64_t second_in_slot;
    uint32_t ms;

    if (time_location == NULL || out_slot_id == NULL || out_ms_into_slot == NULL ||
        (time_location->capabilities & MINI_TIMELOC_CAP_UTC) == 0u ||
        time_location->utc_get == NULL ||
        time_location->utc_get(&utc) != MINI_OK ||
        utc.nanoseconds >= 1000000000u) {
        return false;
    }

    slot_id = utc.unix_seconds / 15;
    second_in_slot = utc.unix_seconds % 15;
    if (second_in_slot < 0) {
        second_in_slot += 15;
        --slot_id;
    }

    ms = (uint32_t)second_in_slot * 1000u + utc.nanoseconds / 1000000u;
    if (ms >= 15000u) return false;

    *out_slot_id = slot_id;
    *out_ms_into_slot = (uint16_t)ms;
    return true;
}

static void remove_pending_cq(AppController *app)
{
    size_t i;

    if (app == NULL) return;
    for (i = 0u; i < auto_seq_active_count(&app->auto_seq); ++i) {
        QsoContext ctx;
        if (!auto_seq_get_active_context(&app->auto_seq, i, &ctx)) return;
        if (ctx.state == AUTO_SEQ_STATE_CALLING &&
            (ctx.flags & AUTO_SEQ_FLAG_FREETEXT) == 0u &&
            strcmp(ctx.dxcall, "CQ") == 0) {
            (void)auto_seq_drop_index(&app->auto_seq, i, 0);
            return;
        }
    }
}

bool app_controller_set_beacon_mode(AppController *app, TxBeaconMode mode)
{
    TxBeaconMode previous;

    if (app == NULL) return false;
    previous = tx_lifecycle_get_beacon_mode(&app->tx.lifecycle);
    if (previous == mode) return true;
    if (!tx_lifecycle_set_beacon_mode(&app->tx.lifecycle, mode)) return false;

    /* A parity/mode change must not leave a stale one-shot CQ in the queue. */
    remove_pending_cq(app);
    return true;
}

TxBeaconMode app_controller_get_beacon_mode(const AppController *app)
{
    return app != NULL ? tx_lifecycle_get_beacon_mode(&app->tx.lifecycle)
                       : TX_BEACON_OFF;
}

bool app_controller_observe_tx_slot(AppController *app,
                                    int64_t slot_id,
                                    uint16_t ms_into_slot,
                                    int64_t now_ms,
                                    bool *out_model_changed)
{
    TxSlotBoundary boundary;
    AutoSeqTxIntent intent;
    AutoSeqLogEvent log_event;
    bool has_boundary;
    bool has_intent;
    AutoSeqResult cq_result;

    if (app == NULL || out_model_changed == NULL) return false;
    *out_model_changed = false;

    if (!tx_lifecycle_observe(&app->tx.lifecycle, slot_id, ms_into_slot,
                              &boundary, &has_boundary)) {
        return false;
    }
    if (!has_boundary) return true;

    has_intent = auto_seq_prepare_tx_intent(&app->auto_seq, &intent);

    /* V2 beacon rule: only create CQ when no QSO/FreeText/CQ intent exists. */
    if (!has_intent && tx_lifecycle_beacon_matches(&app->tx.lifecycle, boundary.parity)) {
        cq_result = auto_seq_start_cq(&app->auto_seq, boundary.parity);
        if (cq_result != AUTO_SEQ_OK && cq_result != AUTO_SEQ_IGNORED)
            return false;
        has_intent = auto_seq_prepare_tx_intent(&app->auto_seq, &intent);
    }

    if (!has_intent || intent.tx_parity != boundary.parity) return true;

    /* TX-start boundary: snapshot intent and logging eligibility before tick. */
    app->tx.last_intent = intent;
    app->tx.last_intent_valid = 1u;
    app->tx.last_tx_slot_id = boundary.slot_id;
    ++app->tx.simulated_tx_count;

    app->tx.last_log_event_valid = 0u;
    if (auto_seq_prepare_log_event(&app->auto_seq, &log_event)) {
        app->tx.last_log_event = log_event;
        app->tx.last_log_event_valid = 1u;
    }

    /* AS-7 transmitter is synchronous simulation: completion immediately ticks policy. */
    if (!auto_seq_tick(&app->auto_seq, now_ms)) return false;

    *out_model_changed = true;
    return true;
}

bool app_controller_step_tx(AppController *app, bool *out_model_changed)
{
    const mini_time_location_api_t *time_location;
    int64_t slot_id;
    uint16_t ms_into_slot;
    int64_t now_ms = 0;

    if (app == NULL || out_model_changed == NULL) return false;
    *out_model_changed = false;

    if (app->api == NULL || app->api->time_location == NULL) return true;
    time_location = app->api->time_location;
    if (!utc_slot_position(time_location, &slot_id, &ms_into_slot)) return true;
    if (time_location->monotonic_us != NULL)
        now_ms = clamp_monotonic_ms(time_location->monotonic_us());

    return app_controller_observe_tx_slot(app, slot_id, ms_into_slot,
                                          now_ms, out_model_changed);
}

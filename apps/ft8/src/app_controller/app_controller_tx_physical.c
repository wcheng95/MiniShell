#define FT8_APP_CONTROLLER_INTERNAL 1
#include "app_controller_tx.h"
#include "tx_offset.h"
#include <limits.h>

static int64_t monotonic_ms(uint64_t us)
{
    uint64_t ms = us / 1000u;
    return ms > INT64_MAX ? INT64_MAX : (int64_t)ms;
}

static bool utc_slot(const mini_time_location_api_t *time, int64_t *slot, uint16_t *ms)
{
    mini_utc_time_t utc = {.struct_size = sizeof(utc)};
    if (!time || !(time->capabilities & MINI_TIMELOC_CAP_UTC) || !time->utc_get ||
        time->utc_get(&utc) != MINI_OK || utc.nanoseconds >= 1000000000u) return false;
    *slot = utc.unix_seconds / 15;
    int64_t seconds = utc.unix_seconds % 15;
    if (seconds < 0) { seconds += 15; --*slot; }
    *ms = (uint16_t)(seconds * 1000 + utc.nanoseconds / 1000000u);
    return true;
}

bool app_controller_tx_active(const AppController *app)
{
    return app && app->tx.active;
}

static void report(const AppController *app, const char *message)
{
    if (app->api && app->api->system && app->api->system->write)
        app->api->system->write(message);
}

static bool fail_tx(AppController *app, const char *message)
{
    ++app->tx.failed_tx_count;
    app->tx.active = app->tx.pending = false;
    report(app, message);
    /* An uncertain begin/end still owes a restoration attempt. Neither failure
     * nor cleanup consumes the semantic intent or its retry counter. */
    mini_result_t restored = app->radio.rx_required ? radio_control_end_tx(&app->radio) : MINI_OK;
    bool resumed = app_controller_resume_rx_after_tx(app);
    if (restored != MINI_OK) report(app, "ft8: TX radio RX restoration failed\n");
    if (!resumed) report(app, "ft8: TX audio restart failed\n");
    return restored == MINI_OK && resumed;
}

static bool advance_tx(AppController *app, uint64_t now, bool *changed)
{
    uint8_t index;
    AppTxDue due = app_tx_schedule_poll(&app->tx.schedule, now, &index);
    if (!app->tx.plan.valid || !app->radio.tx_active || due == APP_TX_CLOCK_ERROR)
        return fail_tx(app, "ft8: TX scheduler state failed\n");
    if (due == APP_TX_WAIT) return true;
    if (due == APP_TX_DONE) {
        if (radio_control_end_tx(&app->radio) != MINI_OK)
            return fail_tx(app, "ft8: TX end failed\n");
        if (!app_controller_resume_rx_after_tx(app)) {
            /* Recovery failed: do not report successful completion or tick. */
            app->tx.active = false;
            ++app->tx.failed_tx_count;
            report(app, "ft8: TX audio restart failed\n");
            return false;
        }
        app_controller_commit_tx_log(app);
        if (!auto_seq_tick(&app->auto_seq, monotonic_ms(now)))
            return fail_tx(app, "ft8: TX semantic completion failed\n");
        app->tx.active = false;
        ++app->tx.physical_tx_count;
        *changed = true;
        return true;
    }
    uint8_t tone = app->tx.plan.tones[index];
    if (tone > 7) return fail_tx(app, "ft8: TX tone plan failed\n");
    if (!app->tx.have_last_tone || tone != app->tx.last_tone) {
        if (radio_control_set_tone_hz(&app->radio, ft8_tx_tone_hz(app->tx.plan.base_hz, tone)) != MINI_OK)
            return fail_tx(app, "ft8: TX tone write failed\n");
        app->tx.have_last_tone = true;
        app->tx.last_tone = tone;
    }
    return true;
}

bool app_controller_step_tx(AppController *app, bool *changed)
{
    if (!app || !changed) return false;
    *changed = false;
    const mini_time_location_api_t *time = app->api ? app->api->time_location : NULL;
    if (app->tx.active) {
        if (!time || !time->monotonic_us) return fail_tx(app, "ft8: TX monotonic clock unavailable\n");
        return advance_tx(app, time->monotonic_us(), changed);
    }
    int64_t slot;
    uint16_t ms;
    if (!utc_slot(time, &slot, &ms)) { app->tx.pending = false; return true; }
    uint64_t now = time->monotonic_us ? time->monotonic_us() : 0;
    if (!app->radio.stream)
        return app_controller_observe_tx_slot(app, slot, ms, monotonic_ms(now), changed);
    if (!time->monotonic_us) return false;

    TxSlotBoundary boundary;
    bool observed;
    if (!tx_lifecycle_observe(&app->tx.lifecycle, slot, ms, &boundary, &observed)) return false;
    if (observed) { app->tx.pending = true; app->tx.pending_slot = slot; }
    if (!app->tx.pending) return true;
    if (app->tx.pending_slot != slot || ms >= TX_LIFECYCLE_BOUNDARY_WINDOW_MS) {
        app->tx.pending = false;
        return true;
    }
    if (!app_controller_rx_ready_for_tx(app, slot)) return true;
    app->tx.pending = false;
    AutoSeqTxIntent intent;
    bool have_intent = auto_seq_prepare_tx_intent(&app->auto_seq, &intent);
    uint8_t parity = (uint8_t)((uint64_t)slot & 1u);
    if (!have_intent && tx_lifecycle_beacon_matches(&app->tx.lifecycle, parity)) {
        AutoSeqResult result = auto_seq_start_cq(&app->auto_seq, parity);
        if (result != AUTO_SEQ_OK && result != AUTO_SEQ_IGNORED)
            return fail_tx(app, "ft8: TX station/intent unavailable\n");
        have_intent = auto_seq_prepare_tx_intent(&app->auto_seq, &intent);
    }
    if (!have_intent || intent.tx_parity != parity) return true;
    if (!tx_offset_resolve(app->config.offset_src, app->config.fixed_offset_hz,
                           &intent, &app->tx.offset_rng, &intent.offset_hz))
        return fail_tx(app, "ft8: TX offset resolution failed\n");
    app->tx.last_intent = intent;
    app->tx.last_intent_valid = true;
    app->tx.last_tx_slot_id = slot;
    /* Even a free-text attempt requires the loaded station identity to encode. */
    AutoSeqTxIntent identity = intent;
    identity.type = AUTO_SEQ_TX_INTENT_CQ;
    identity.cq_type = AUTO_SEQ_CQ;
    Ft8TxPlan identity_plan;
    app->tx.plan.valid = false;
    if (ft8_tx_encode(&identity, &identity_plan) != FT8_TX_ENCODE_OK ||
        ft8_tx_encode(&intent, &app->tx.plan) != FT8_TX_ENCODE_OK)
        return fail_tx(app, "ft8: TX plan/identity encoding failed\n");
    if (!app_tx_schedule_begin(&app->tx.schedule, now, ms))
        return fail_tx(app, "ft8: TX slot anchor failed\n");
    app_controller_prepare_tx_log(app);
    if (!app_controller_pause_rx_for_tx(app)) return fail_tx(app, "ft8: TX audio pause failed\n");
    if (app->config.rxtx_log && !log_service_write_rt(&app->log, true, app->config.band_index,
                                                     app->tx.plan.canonical_text, 0, app->tx.plan.base_hz))
        return fail_tx(app, "ft8: TX RT log failed; transmission skipped\n");
    /* Filesystem/Audio work must not turn an early candidate into a late key-up. */
    uint64_t prepared = time->monotonic_us();
    if (prepared < now || prepared - app->tx.schedule.slot_start_us >=
        (uint64_t)TX_LIFECYCLE_BOUNDARY_WINDOW_MS * 1000u)
        return fail_tx(app, "ft8: TX preparation missed start window\n");
    if (radio_control_begin_tx(&app->radio) != MINI_OK)
        return fail_tx(app, "ft8: TX begin failed\n");
    app->tx.active = true;
    app->tx.have_last_tone = false;
    /* Begin writes may take time: skip to the current absolute tone, never drift. */
    return advance_tx(app, time->monotonic_us(), changed);
}

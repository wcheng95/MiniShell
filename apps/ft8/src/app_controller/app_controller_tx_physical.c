#define FT8_APP_CONTROLLER_INTERNAL 1
#include "app_controller_tx.h"
#include "tx_offset.h"
#include <limits.h>
#include <stdio.h>

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

static void milestone(const AppController *app, const char *stage, mini_result_t code)
{
    char text[128];
    snprintf(text, sizeof(text), "ft8: FT8T %s code=%d slot=%lld\n",
             stage, (int)code, (long long)app->tx.last_tx_slot_id);
    report(app, text);
}

static void consume_band_sync_slot(AppController *app)
{
    const mini_time_location_api_t *time = app->api ? app->api->time_location : NULL;
    int64_t slot;
    uint16_t ms;
    TxSlotBoundary boundary;
    bool observed;
    app->tx.pending = false;
    if (utc_slot(time, &slot, &ms))
        (void)tx_lifecycle_observe(&app->tx.lifecycle, slot, ms, &boundary, &observed);
    else
        app->tx.lifecycle.have_observed_slot = 0; // Re-anchor when UTC returns.
}

bool app_controller_step_cat(AppController *app)
{
    if (!app) return false;
    if (app->cat_band_sync_failed) return true;
    if (!app->cat_band_sync_pending) return true;
    consume_band_sync_slot(app);
    const mini_time_location_api_t *time = app->api ? app->api->time_location : NULL;
    if (time && time->monotonic_us && app->cat_band_changed_us != UINT64_MAX) {
        uint64_t now = time->monotonic_us();
        if (now >= app->cat_band_changed_us && now - app->cat_band_changed_us < 1000000u)
            return true;
    }
    mini_result_t result = radio_control_sync_frequency(&app->radio,
            config_service_band_dial_hz(app->config.band_index));
    if (result != MINI_OK) {
        app->cat_band_sync_failed = true;
        report(app, "ft8: band CAT command-submission failure; radio state unknown\n");
        milestone(app, "FAULT stage=band", result);
        return true;
    }
    // Also consume a boundary crossed by blocking CAT writes, or by a loop
    // that first observes expiry in the new slot. Never key up late after sync.
    consume_band_sync_slot(app);
    app->cat_band_sync_pending = false;
    return true;
}

static mini_result_t restore_radio(AppController *app)
{
    if (!app->radio.rx_required) return MINI_OK;
    milestone(app, "END_ENTER", MINI_OK);
    mini_result_t result = radio_control_end_tx(&app->radio);
    milestone(app, result == MINI_OK ? "END_OK" : "FAULT stage=end", result);
    return result;
}

static bool resume_audio(AppController *app)
{
    bool resumed = app_controller_resume_rx_after_tx(app);
    milestone(app, resumed ? "RX_RESUME" : "FAULT stage=audio_resume",
              resumed ? MINI_OK : MINI_ERR_IO);
    return resumed;
}

static void abort_tx(AppController *app, bool restore)
{
    ++app->tx.failed_tx_count;
    app->tx.active = app->tx.pending = false;
    /* One best-effort restore, never retry an ambiguous RX command. The radio
     * and Audio owners retain uncertainty/paused state on recovery failure. */
    if (restore) (void)restore_radio(app);
    (void)resume_audio(app);
}

static bool transport_fault(AppController *app, const char *stage, mini_result_t code,
                            bool restore)
{
    milestone(app, stage, code);
    abort_tx(app, restore);
    return true; // Transport faults never own the application/UI lifecycle.
}

static bool fail_tx(AppController *app, const char *message)
{
    report(app, message);
    abort_tx(app, true);
    return false; // Internal invariant/semantic failures remain errors.
}

static bool advance_tx(AppController *app, uint64_t now, bool *changed)
{
    uint8_t index;
    AppTxDue due = app_tx_schedule_poll(&app->tx.schedule, now, &index);
    if (!app->tx.plan.valid || !app->radio.tx_active || due == APP_TX_CLOCK_ERROR)
        return fail_tx(app, "ft8: TX scheduler state failed\n");
    if (due == APP_TX_WAIT) return true;
    if (due == APP_TX_DONE) {
        if (restore_radio(app) != MINI_OK) {
            abort_tx(app, false);
            return true;
        }
        if (!resume_audio(app)) {
            /* Recovery failed: do not report successful completion or tick. */
            app->tx.active = app->tx.pending = false;
            ++app->tx.failed_tx_count;
            return true;
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
        mini_result_t result = radio_control_set_tone_hz(&app->radio,
            ft8_tx_tone_hz(app->tx.plan.base_hz, tone));
        if (result != MINI_OK)
            return transport_fault(app, app->tx.have_last_tone ? "FAULT stage=tone" :
                                   "FAULT stage=first_tone", result, true);
        if (!app->tx.have_last_tone) milestone(app, "FIRST_TONE_OK", MINI_OK);
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
    if (app->cat_band_sync_pending || app->cat_band_sync_failed ||
        app->radio.rx_required || app->tx.rx_paused) {
        app->tx.pending = false;
        return true;
    }
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
    milestone(app, "PREP", MINI_OK);
    app_controller_prepare_tx_log(app);
    if (!app_controller_pause_rx_for_tx(app)) return transport_fault(app, "FAULT stage=audio_pause", MINI_ERR_IO, false);
    if (app->config.rxtx_log && !log_service_write_rt(&app->log, true, app->config.band_index,
                                                     app->tx.plan.canonical_text, 0, app->tx.plan.base_hz))
        return transport_fault(app, "FAULT stage=rt_log", MINI_ERR_IO, false);
    /* Filesystem/Audio work must not turn an early candidate into a late key-up. */
    uint64_t prepared = time->monotonic_us();
    if (prepared < now) return fail_tx(app, "ft8: TX preparation clock reversed\n");
    if (prepared - app->tx.schedule.slot_start_us >=
        (uint64_t)TX_LIFECYCLE_BOUNDARY_WINDOW_MS * 1000u)
        return transport_fault(app, "FAULT stage=prepare_window", MINI_ERR_TIMEOUT, false);
    milestone(app, "BEGIN_ENTER", MINI_OK);
    mini_result_t result = radio_control_begin_tx(&app->radio);
    if (result != MINI_OK)
        return transport_fault(app, "FAULT stage=begin", result, true);
    milestone(app, "BEGIN_OK", MINI_OK);
    app->tx.active = true;
    app->tx.have_last_tone = false;
    /* Begin writes may take time: skip to the current absolute tone, never drift. */
    return advance_tx(app, time->monotonic_us(), changed);
}

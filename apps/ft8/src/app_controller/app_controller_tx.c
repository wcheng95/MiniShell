#include "app_controller_tx.h"

#include <limits.h>
#include <string.h>

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
    if (app->tx.active) return true;
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

    app_controller_prepare_tx_log(app);
    app_controller_commit_tx_log(app);

    /* AS-7 transmitter is synchronous simulation: completion immediately ticks policy. */
    if (!auto_seq_tick(&app->auto_seq, now_ms)) return false;

    *out_model_changed = true;
    return true;
}

void app_controller_prepare_tx_log(AppController *app)
{
    app->tx.last_log_event_valid = auto_seq_prepare_log_event(&app->auto_seq, &app->tx.last_log_event);
}

void app_controller_commit_tx_log(AppController *app)
{
    if (!app->tx.last_log_event_valid) return;
    const AutoSeqLogEvent *log_event = &app->tx.last_log_event;
    bool adif_written = false;
    bool cabrillo_written = false;

    const LogStationFacts station = {
        .callsign = app->config.callsign,
        .effective_grid = app->effective_grid,
        .fd_exchange = app->config.fd_exchange,
        .band_index = app->config.band_index
    };
    const LogQsoFacts facts = {
        .dxcall = log_event->dxcall,
        .dxgrid = log_event->dxgrid,
        .fd_rx_exchange = log_event->fd_rx_exchange,
        .snr_tx = log_event->snr_tx,
        .snr_rx = log_event->snr_rx,
        .snr_tx_known = log_event->snr_tx != AUTO_SEQ_SNR_UNKNOWN,
        .snr_rx_known = log_event->snr_rx != AUTO_SEQ_SNR_UNKNOWN
    };

    if (log_event->adif_eligible != 0u)
        adif_written = log_service_write_adif(&app->log, &station, &facts);
    if (log_event->cabrillo_fd_eligible != 0u)
        cabrillo_written = log_service_write_cabrillo(&app->log, &station, &facts);

    if (adif_written && app->qso_loaded) app->qso_dirty = true;

    /* Preserve V2 no-duplicate behavior: mark each log type only after its
     * corresponding MiniShell FS write completed successfully. */
    (void)auto_seq_ack_log_event(&app->auto_seq, log_event,
                                 adif_written, cabrillo_written);
}

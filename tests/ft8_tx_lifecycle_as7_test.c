#include <stdio.h>
#include <string.h>

#include "app_controller_internal.h"
#include "app_controller_tx.h"
#include "auto_seq_tx_intent.h"
#include "tx_lifecycle.h"

#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        return 1; \
    } \
} while (0)

static AutoSeqRxEvent event_for(const char *dxcall, AutoSeqMessageKind kind,
                                int64_t rx_slot)
{
    AutoSeqRxEvent event;
    memset(&event, 0, sizeof(event));
    snprintf(event.dxcall, sizeof(event.dxcall), "%s", dxcall);
    event.kind = kind;
    event.rx_slot_id = rx_slot;
    event.offset_hz = 1500;
    event.snr_db = -12;
    event.report_db = AUTO_SEQ_SNR_UNKNOWN;
    return event;
}

static int init_app(AppController *app)
{
    memset(app, 0, sizeof(*app));
    CHECK(auto_seq_init(&app->auto_seq, NULL));
    CHECK(auto_seq_set_station(&app->auto_seq, "AG6AQ", "CM97"));
    tx_lifecycle_init(&app->tx.lifecycle);
    return 0;
}

static int test_slot_gate(void)
{
    TxLifecycle lifecycle;
    TxSlotBoundary boundary;
    bool has_boundary = false;

    tx_lifecycle_init(&lifecycle);
    CHECK(tx_lifecycle_get_beacon_mode(&lifecycle) == TX_BEACON_OFF);
    CHECK(tx_lifecycle_set_beacon_mode(&lifecycle, TX_BEACON_EVEN));
    CHECK(tx_lifecycle_beacon_matches(&lifecycle, 0u));
    CHECK(!tx_lifecycle_beacon_matches(&lifecycle, 1u));

    CHECK(tx_lifecycle_observe(&lifecycle, 100, 500u, &boundary, &has_boundary));
    CHECK(!has_boundary); /* first observation anchors only */
    CHECK(tx_lifecycle_observe(&lifecycle, 100, 900u, &boundary, &has_boundary));
    CHECK(!has_boundary);

    CHECK(tx_lifecycle_observe(&lifecycle, 101, 20u, &boundary, &has_boundary));
    CHECK(has_boundary);
    CHECK(boundary.slot_id == 101);
    CHECK(boundary.parity == 1u);

    CHECK(tx_lifecycle_observe(&lifecycle, 101, 400u, &boundary, &has_boundary));
    CHECK(!has_boundary); /* duplicate observation cannot retransmit */

    CHECK(tx_lifecycle_observe(&lifecycle, 103, 20u, &boundary, &has_boundary));
    CHECK(!has_boundary); /* missed slot: re-anchor, never catch up */
    CHECK(tx_lifecycle_observe(&lifecycle, 102, 20u, &boundary, &has_boundary));
    CHECK(!has_boundary); /* backward UTC correction: re-anchor only */
    CHECK(tx_lifecycle_observe(&lifecycle, 103, 1200u, &boundary, &has_boundary));
    CHECK(!has_boundary); /* adjacent but observed too late */
    CHECK(tx_lifecycle_observe(&lifecycle, 104, 10u, &boundary, &has_boundary));
    CHECK(has_boundary);
    CHECK(boundary.parity == 0u);
    return 0;
}

static int test_intent_projection(void)
{
    AutoSeq seq;
    AutoSeqRxEvent event;
    AutoSeqTxIntent intent;

    CHECK(auto_seq_init(&seq, NULL));
    CHECK(auto_seq_set_station(&seq, "ag6aq", "cm97"));
    auto_seq_set_max_retry(&seq, 3);

    event = event_for("W1XYZ", AUTO_SEQ_MSG_TX1, 77);
    event.flags = AUTO_SEQ_RX_FLAG_CQ;
    event.offset_hz = 1425;
    event.snr_db = -17;
    snprintf(event.dxgrid, sizeof(event.dxgrid), "%s", "FN42");
    CHECK(auto_seq_on_manual_rx(&seq, &event) == AUTO_SEQ_OK);
    CHECK(auto_seq_prepare_tx_intent(&seq, &intent));
    CHECK(intent.type == AUTO_SEQ_TX_INTENT_QSO);
    CHECK(intent.message_kind == AUTO_SEQ_MSG_TX1);
    CHECK(intent.tx_parity == 0u);
    CHECK(intent.offset_hz == 1425);
    CHECK(intent.report_db == -17);
    CHECK(intent.retry_counter == 0u && intent.retry_limit == 3u);
    CHECK(strcmp(intent.callsign, "AG6AQ") == 0);
    CHECK(strcmp(intent.grid, "CM97") == 0);
    CHECK(strcmp(intent.dxcall, "W1XYZ") == 0);
    CHECK(strcmp(intent.dxgrid, "FN42") == 0);

    CHECK(auto_seq_init(&seq, NULL));
    CHECK(auto_seq_set_station(&seq, "AG6AQ", "CM97"));
    CHECK(auto_seq_set_fd_exchange(&seq, "1b scv"));
    event = event_for("W6ABC", AUTO_SEQ_MSG_TX1, 91);
    event.flags = AUTO_SEQ_RX_FLAG_CQ | AUTO_SEQ_RX_FLAG_FD;
    CHECK(auto_seq_on_manual_rx(&seq, &event) == AUTO_SEQ_OK);
    CHECK(auto_seq_prepare_tx_intent(&seq, &intent));
    CHECK(intent.type == AUTO_SEQ_TX_INTENT_QSO);
    CHECK(intent.message_kind == AUTO_SEQ_MSG_TX2);
    CHECK((intent.flags & AUTO_SEQ_TX_INTENT_FLAG_FD) != 0u);
    CHECK(strcmp(intent.fd_exchange, "1B SCV") == 0);

    CHECK(auto_seq_init(&seq, NULL));
    CHECK(auto_seq_set_station(&seq, "AG6AQ", "CM97"));
    CHECK(auto_seq_set_cq(&seq, AUTO_SEQ_CQ_SOTA, ""));
    CHECK(auto_seq_start_cq(&seq, 0u) == AUTO_SEQ_OK);
    CHECK(auto_seq_prepare_tx_intent(&seq, &intent));
    CHECK(intent.type == AUTO_SEQ_TX_INTENT_CQ);
    CHECK(intent.message_kind == AUTO_SEQ_MSG_TX6);
    CHECK(intent.cq_type == AUTO_SEQ_CQ_SOTA);
    CHECK(intent.tx_parity == 0u);

    CHECK(auto_seq_init(&seq, NULL));
    CHECK(auto_seq_set_station(&seq, "AG6AQ", "CM97"));
    CHECK(auto_seq_set_cq(&seq, AUTO_SEQ_CQ_FREETEXT, "CQ TEST"));
    CHECK(auto_seq_start_cq(&seq, 1u) == AUTO_SEQ_OK);
    CHECK(auto_seq_prepare_tx_intent(&seq, &intent));
    CHECK(intent.type == AUTO_SEQ_TX_INTENT_CQ);
    CHECK(intent.cq_type == AUTO_SEQ_CQ_FREETEXT);
    CHECK(strcmp(intent.text, "CQ TEST") == 0);

    CHECK(auto_seq_init(&seq, NULL));
    CHECK(auto_seq_set_station(&seq, "AG6AQ", "CM97"));
    CHECK(auto_seq_schedule_freetext(&seq, "TNX 73", 1u) == AUTO_SEQ_OK);
    CHECK(auto_seq_prepare_tx_intent(&seq, &intent));
    CHECK(intent.type == AUTO_SEQ_TX_INTENT_FREETEXT);
    CHECK(intent.message_kind == AUTO_SEQ_MSG_NONE);
    CHECK(intent.tx_parity == 1u);
    CHECK(strcmp(intent.text, "TNX 73") == 0);
    return 0;
}

static int test_controller_qso_completion(void)
{
    AppController app;
    AutoSeqRxEvent event;
    QsoContext ctx;
    bool changed = false;

    CHECK(init_app(&app) == 0);
    auto_seq_set_max_retry(&app.auto_seq, 1);

    /* Anchor in slot 100; never transmit merely because the app started mid-slot. */
    CHECK(app_controller_observe_tx_slot(&app, 100, 500u, 1000, &changed));
    CHECK(!changed);
    CHECK(app.tx.simulated_tx_count == 0u);

    event = event_for("N6HAN", AUTO_SEQ_MSG_TX1, 100);
    event.flags = AUTO_SEQ_RX_FLAG_CQ;
    CHECK(auto_seq_on_manual_rx(&app.auto_seq, &event) == AUTO_SEQ_OK);

    /* RX slot 100 creates odd TX parity, so slot 101 sends first TX1. */
    CHECK(app_controller_observe_tx_slot(&app, 101, 10u, 2000, &changed));
    CHECK(changed);
    CHECK(app.tx.simulated_tx_count == 1u);
    CHECK(app.tx.last_tx_slot_id == 101);
    CHECK(app.tx.last_intent_valid != 0u);
    CHECK(app.tx.last_intent.type == AUTO_SEQ_TX_INTENT_QSO);
    CHECK(app.tx.last_intent.message_kind == AUTO_SEQ_MSG_TX1);
    CHECK(strcmp(app.tx.last_intent.dxcall, "N6HAN") == 0);
    CHECK(auto_seq_get_active_context(&app.auto_seq, 0u, &ctx));
    CHECK(ctx.retry_counter == 1u);

    /* Duplicate observation and opposite parity do not consume another retry. */
    CHECK(app_controller_observe_tx_slot(&app, 101, 500u, 2500, &changed));
    CHECK(!changed);
    CHECK(app_controller_observe_tx_slot(&app, 102, 10u, 3000, &changed));
    CHECK(!changed);
    CHECK(app.tx.simulated_tx_count == 1u);

    /* Second matching TX completes retry exhaustion and removes pre-exchange QSO. */
    CHECK(app_controller_observe_tx_slot(&app, 103, 10u, 4000, &changed));
    CHECK(changed);
    CHECK(app.tx.simulated_tx_count == 2u);
    CHECK(auto_seq_active_count(&app.auto_seq) == 0u);
    CHECK(auto_seq_inactive_count(&app.auto_seq) == 0u);
    return 0;
}

static int test_controller_logging_start_boundary(void)
{
    AppController app;
    AutoSeqRxEvent event;
    bool changed = false;

    CHECK(init_app(&app) == 0);
    CHECK(app_controller_observe_tx_slot(&app, 200, 500u, 1000, &changed));

    /* Build a ROGERS context whose next semantic TX is TX4/RR73. */
    event = event_for("K9XYZ", AUTO_SEQ_MSG_TX1, 200);
    event.flags = AUTO_SEQ_RX_FLAG_CQ;
    auto_seq_set_skip_tx1(&app.auto_seq, true);
    CHECK(auto_seq_on_manual_rx(&app.auto_seq, &event) == AUTO_SEQ_OK);
    event = event_for("K9XYZ", AUTO_SEQ_MSG_TX3, 202);
    event.flags = AUTO_SEQ_RX_FLAG_TO_ME;
    event.report_db = -7;
    CHECK(auto_seq_on_addressed_rx(&app.auto_seq, &event) == AUTO_SEQ_OK);

    /* RX slot 202 => TX parity odd; logging eligibility is captured before tick. */
    CHECK(app_controller_observe_tx_slot(&app, 201, 10u, 2000, &changed));
    CHECK(changed);
    CHECK(app.tx.last_intent.message_kind == AUTO_SEQ_MSG_TX4);
    CHECK(app.tx.last_log_event_valid != 0u);
    CHECK(app.tx.last_log_event.tx_kind == AUTO_SEQ_MSG_TX4);
    CHECK(app.tx.last_log_event.adif_eligible != 0u);
    return 0;
}

static int test_beacon_lifecycle_and_preemption(void)
{
    AppController app;
    AutoSeqRxEvent event;
    QsoContext ctx;
    bool changed = false;

    CHECK(init_app(&app) == 0);
    CHECK(app_controller_set_beacon_mode(&app, TX_BEACON_EVEN));
    CHECK(app_controller_get_beacon_mode(&app) == TX_BEACON_EVEN);

    CHECK(app_controller_observe_tx_slot(&app, 199, 500u, 1000, &changed));
    CHECK(!changed);
    CHECK(app_controller_observe_tx_slot(&app, 200, 10u, 2000, &changed));
    CHECK(changed);
    CHECK(app.tx.last_intent.type == AUTO_SEQ_TX_INTENT_CQ);
    CHECK(app.tx.last_intent.tx_parity == 0u);
    CHECK(auto_seq_active_count(&app.auto_seq) == 0u); /* one-shot popped by tick */

    CHECK(app_controller_observe_tx_slot(&app, 201, 10u, 3000, &changed));
    CHECK(!changed);
    CHECK(app_controller_observe_tx_slot(&app, 202, 10u, 4000, &changed));
    CHECK(changed);
    CHECK(app.tx.simulated_tx_count == 2u); /* fresh CQ re-enqueued */

    /* Active QSO prevents beacon enqueue and transmits on its own matching parity. */
    event = event_for("N6HAN", AUTO_SEQ_MSG_TX1, 203); /* next TX parity even */
    event.flags = AUTO_SEQ_RX_FLAG_CQ;
    CHECK(auto_seq_on_manual_rx(&app.auto_seq, &event) == AUTO_SEQ_OK);
    CHECK(app_controller_observe_tx_slot(&app, 203, 10u, 5000, &changed));
    CHECK(!changed); /* wrong parity for QSO; beacon does not bypass it */
    CHECK(auto_seq_active_count(&app.auto_seq) == 1u);
    CHECK(app_controller_observe_tx_slot(&app, 204, 10u, 6000, &changed));
    CHECK(changed);
    CHECK(app.tx.last_intent.type == AUTO_SEQ_TX_INTENT_QSO);
    CHECK(strcmp(app.tx.last_intent.dxcall, "N6HAN") == 0);

    /* Mode change removes an already queued stale CQ immediately. */
    CHECK(auto_seq_start_cq(&app.auto_seq, 0u) == AUTO_SEQ_OK);
    CHECK(auto_seq_active_count(&app.auto_seq) >= 1u);
    CHECK(app_controller_set_beacon_mode(&app, TX_BEACON_OFF));
    CHECK(app_controller_get_beacon_mode(&app) == TX_BEACON_OFF);
    CHECK(auto_seq_get_active_context(&app.auto_seq, 0u, &ctx));
    CHECK(strcmp(ctx.dxcall, "CQ") != 0);
    return 0;
}

static int test_no_catchup_after_missed_slot(void)
{
    AppController app;
    AutoSeqRxEvent event;
    bool changed = false;

    CHECK(init_app(&app) == 0);
    event = event_for("W7ABC", AUTO_SEQ_MSG_TX1, 300); /* odd TX */
    event.flags = AUTO_SEQ_RX_FLAG_CQ;
    CHECK(auto_seq_on_manual_rx(&app.auto_seq, &event) == AUTO_SEQ_OK);

    CHECK(app_controller_observe_tx_slot(&app, 300, 500u, 1000, &changed));
    CHECK(!changed);
    CHECK(app_controller_observe_tx_slot(&app, 302, 10u, 2000, &changed));
    CHECK(!changed); /* slot 301 was missed; never catch up */
    CHECK(app.tx.simulated_tx_count == 0u);
    CHECK(app_controller_observe_tx_slot(&app, 303, 10u, 3000, &changed));
    CHECK(changed);
    CHECK(app.tx.simulated_tx_count == 1u);
    return 0;
}

int main(void)
{
    CHECK(test_slot_gate() == 0);
    CHECK(test_intent_projection() == 0);
    CHECK(test_controller_qso_completion() == 0);
    CHECK(test_controller_logging_start_boundary() == 0);
    CHECK(test_beacon_lifecycle_and_preemption() == 0);
    CHECK(test_no_catchup_after_missed_slot() == 0);

    puts("ft8_tx_lifecycle_as7_test: PASS");
    return 0;
}

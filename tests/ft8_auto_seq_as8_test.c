#include <stdio.h>
#include <string.h>

#include "auto_seq.h"

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

static int expect_head(const AutoSeq *seq, const char *dxcall,
                       AutoSeqState state, AutoSeqMessageKind next_tx,
                       uint16_t retry_counter)
{
    AutoSeqQsoView view;
    CHECK(auto_seq_snapshot_active(seq, &view, 1u) == 1u);
    CHECK(strcmp(view.dxcall, dxcall) == 0);
    CHECK(view.state == state);
    CHECK(view.next_tx == next_tx);
    CHECK(view.retry_counter == retry_counter);
    return 0;
}

static int test_normal_qso_progression(void)
{
    AutoSeq seq;
    AutoSeqRxEvent event;

    CHECK(auto_seq_init(&seq, NULL));
    CHECK(auto_seq_set_station(&seq, "AG6AQ", "CM97"));

    event = event_for("N6HAN", AUTO_SEQ_MSG_TX1, 1);
    event.flags = AUTO_SEQ_RX_FLAG_CQ;
    event.snr_db = -5;
    snprintf(event.dxgrid, sizeof(event.dxgrid), "%s", "CM87");
    CHECK(auto_seq_on_manual_rx(&seq, &event) == AUTO_SEQ_OK);
    CHECK(expect_head(&seq, "N6HAN", AUTO_SEQ_STATE_REPLYING,
                      AUTO_SEQ_MSG_TX1, 0u) == 0);

    event = event_for("N6HAN", AUTO_SEQ_MSG_TX2, 3);
    event.flags = AUTO_SEQ_RX_FLAG_TO_ME;
    event.report_db = -12;
    CHECK(auto_seq_on_addressed_rx(&seq, &event) == AUTO_SEQ_OK);
    CHECK(expect_head(&seq, "N6HAN", AUTO_SEQ_STATE_ROGER_REPORT,
                      AUTO_SEQ_MSG_TX3, 0u) == 0);

    event = event_for("N6HAN", AUTO_SEQ_MSG_TX4, 5);
    event.flags = AUTO_SEQ_RX_FLAG_TO_ME;
    CHECK(auto_seq_on_addressed_rx(&seq, &event) == AUTO_SEQ_OK);
    CHECK(expect_head(&seq, "N6HAN", AUTO_SEQ_STATE_SIGNOFF,
                      AUTO_SEQ_MSG_TX5, 0u) == 0);
    return 0;
}

static int test_report_deadlock_equivalence(void)
{
    AutoSeq seq;
    AutoSeqRxEvent event;
    QsoContext ctx;

    CHECK(auto_seq_init(&seq, NULL));

    /* V2 report-deadlock regression: fresh addressed TX1 starts REPORT/TX2. */
    event = event_for("N6HAN", AUTO_SEQ_MSG_TX1, 1);
    event.flags = AUTO_SEQ_RX_FLAG_TO_ME;
    event.snr_db = -10;
    snprintf(event.dxgrid, sizeof(event.dxgrid), "%s", "CM87");
    CHECK(auto_seq_on_addressed_rx(&seq, &event) == AUTO_SEQ_OK);
    CHECK(expect_head(&seq, "N6HAN", AUTO_SEQ_STATE_REPORT,
                      AUTO_SEQ_MSG_TX2, 0u) == 0);

    /* REPORT + a plain report must advance to TX3 rather than stall. */
    event = event_for("N6HAN", AUTO_SEQ_MSG_TX2, 3);
    event.flags = AUTO_SEQ_RX_FLAG_TO_ME;
    event.snr_db = -7;
    event.report_db = -7;
    CHECK(auto_seq_on_addressed_rx(&seq, &event) == AUTO_SEQ_OK);
    CHECK(expect_head(&seq, "N6HAN", AUTO_SEQ_STATE_ROGER_REPORT,
                      AUTO_SEQ_MSG_TX3, 0u) == 0);
    CHECK(auto_seq_get_active_context(&seq, 0u, &ctx));
    CHECK(ctx.snr_tx == -10);
    CHECK(ctx.snr_rx == -7);
    return 0;
}

static int test_reactivation_deadlock_equivalence(void)
{
    AutoSeq seq;
    AutoSeqRxEvent event;
    QsoContext ctx;

    CHECK(auto_seq_init(&seq, NULL));
    auto_seq_set_max_retry(&seq, 2);

    event = event_for("KG4OJT", AUTO_SEQ_MSG_TX1, 1);
    event.flags = AUTO_SEQ_RX_FLAG_TO_ME;
    event.snr_db = -5;
    snprintf(event.dxgrid, sizeof(event.dxgrid), "%s", "FM18");
    CHECK(auto_seq_on_addressed_rx(&seq, &event) == AUTO_SEQ_OK);
    CHECK(expect_head(&seq, "KG4OJT", AUTO_SEQ_STATE_REPORT,
                      AUTO_SEQ_MSG_TX2, 0u) == 0);

    CHECK(auto_seq_tick(&seq, 1000));
    CHECK(auto_seq_tick(&seq, 2000));
    CHECK(auto_seq_tick(&seq, 3000));
    CHECK(auto_seq_active_count(&seq) == 0u);
    CHECK(auto_seq_inactive_count(&seq) == 1u);
    CHECK(auto_seq_get_inactive_context(&seq, 0u, &ctx));
    CHECK(ctx.state == AUTO_SEQ_STATE_REPORT);
    CHECK(ctx.snr_tx == -5);

    /* Late TX1 reactivates the parked REPORT context and must resume TX2. */
    event.rx_slot_id = 11;
    CHECK(auto_seq_on_addressed_rx(&seq, &event) == AUTO_SEQ_OK);
    CHECK(auto_seq_inactive_count(&seq) == 0u);
    CHECK(expect_head(&seq, "KG4OJT", AUTO_SEQ_STATE_REPORT,
                      AUTO_SEQ_MSG_TX2, 0u) == 0);
    CHECK(auto_seq_get_active_context(&seq, 0u, &ctx));
    CHECK(ctx.snr_tx == -5);
    CHECK(ctx.inactive_since_ms == 0);
    return 0;
}

static int test_reincarnation_guards(void)
{
    AutoSeq seq;
    AutoSeqRxEvent event;

    CHECK(auto_seq_init(&seq, NULL));

    event = event_for("W6ABC", AUTO_SEQ_MSG_TX3, 13);
    event.flags = AUTO_SEQ_RX_FLAG_TO_ME;
    event.report_db = -8;
    CHECK(auto_seq_on_addressed_rx(&seq, &event) == AUTO_SEQ_IGNORED);
    CHECK(auto_seq_active_count(&seq) == 0u);

    event.kind = AUTO_SEQ_MSG_TX4;
    event.report_db = AUTO_SEQ_SNR_UNKNOWN;
    CHECK(auto_seq_on_addressed_rx(&seq, &event) == AUTO_SEQ_IGNORED);
    event.kind = AUTO_SEQ_MSG_TX5;
    CHECK(auto_seq_on_addressed_rx(&seq, &event) == AUTO_SEQ_IGNORED);
    CHECK(auto_seq_total_count(&seq) == 0u);

    /* Unknown TX2 remains a valid fresh start because it preserves both reports. */
    event.kind = AUTO_SEQ_MSG_TX2;
    event.snr_db = -9;
    event.report_db = -7;
    CHECK(auto_seq_on_addressed_rx(&seq, &event) == AUTO_SEQ_OK);
    CHECK(expect_head(&seq, "W6ABC", AUTO_SEQ_STATE_ROGER_REPORT,
                      AUTO_SEQ_MSG_TX3, 0u) == 0);
    return 0;
}

static int test_freetext_preemption_equivalence(void)
{
    AutoSeq seq;
    AutoSeqRxEvent event;
    AutoSeqQsoView views[2];

    CHECK(auto_seq_init(&seq, NULL));
    event = event_for("N6HAN", AUTO_SEQ_MSG_TX1, 3);
    event.flags = AUTO_SEQ_RX_FLAG_CQ;
    CHECK(auto_seq_on_manual_rx(&seq, &event) == AUTO_SEQ_OK);

    CHECK(auto_seq_schedule_freetext(&seq, "HELLO WORLD", 1u) == AUTO_SEQ_OK);
    CHECK(auto_seq_snapshot_active(&seq, views, 2u) == 2u);
    CHECK(strcmp(views[0].dxcall, "(FT)") == 0);
    CHECK((views[0].flags & AUTO_SEQ_FLAG_FREETEXT) != 0u);
    CHECK(views[0].tx_parity == views[1].tx_parity);
    CHECK(strcmp(views[1].dxcall, "N6HAN") == 0);

    CHECK(auto_seq_tick(&seq, 4000));
    CHECK(auto_seq_active_count(&seq) == 1u);
    CHECK(expect_head(&seq, "N6HAN", AUTO_SEQ_STATE_REPLYING,
                      AUTO_SEQ_MSG_TX1, 0u) == 0);
    return 0;
}

static int test_fd_signoff_reentry_idempotence(void)
{
    AutoSeq seq;
    AutoSeqRxEvent event;
    AutoSeqLogEvent log_event;

    CHECK(auto_seq_init(&seq, NULL));
    CHECK(auto_seq_set_station(&seq, "AG6AQ", "CM97"));
    CHECK(auto_seq_set_fd_exchange(&seq, "1A SCV"));

    /* Intentional V3 difference: selecting CQ FD starts directly at TX2. */
    event = event_for("W6ABC", AUTO_SEQ_MSG_TX1, 1);
    event.flags = AUTO_SEQ_RX_FLAG_CQ | AUTO_SEQ_RX_FLAG_FD;
    event.snr_db = -8;
    snprintf(event.dxgrid, sizeof(event.dxgrid), "%s", "CM88");
    CHECK(auto_seq_on_manual_rx(&seq, &event) == AUTO_SEQ_OK);
    CHECK(expect_head(&seq, "W6ABC", AUTO_SEQ_STATE_REPORT,
                      AUTO_SEQ_MSG_TX2, 0u) == 0);

    event = event_for("W6ABC", AUTO_SEQ_MSG_TX2, 3);
    event.flags = AUTO_SEQ_RX_FLAG_TO_ME | AUTO_SEQ_RX_FLAG_FD;
    snprintf(event.fd_exchange, sizeof(event.fd_exchange), "%s", "2A ORG");
    CHECK(auto_seq_on_addressed_rx(&seq, &event) == AUTO_SEQ_OK);
    CHECK(expect_head(&seq, "W6ABC", AUTO_SEQ_STATE_ROGER_REPORT,
                      AUTO_SEQ_MSG_TX3, 0u) == 0);

    event = event_for("W6ABC", AUTO_SEQ_MSG_TX4, 5);
    event.flags = AUTO_SEQ_RX_FLAG_TO_ME | AUTO_SEQ_RX_FLAG_FD;
    CHECK(auto_seq_on_addressed_rx(&seq, &event) == AUTO_SEQ_OK);
    CHECK(expect_head(&seq, "W6ABC", AUTO_SEQ_STATE_SIGNOFF,
                      AUTO_SEQ_MSG_TX5, 0u) == 0);

    CHECK(auto_seq_prepare_log_event(&seq, &log_event));
    CHECK(log_event.tx_kind == AUTO_SEQ_MSG_TX5);
    CHECK(log_event.adif_eligible != 0u);
    CHECK(log_event.cabrillo_fd_eligible != 0u);
    CHECK(auto_seq_ack_log_event(&seq, &log_event, true, true));
    CHECK(!auto_seq_prepare_log_event(&seq, &log_event));

    /* TX5 completion parks metadata; a late RR73 reuses it without re-logging. */
    CHECK(auto_seq_tick(&seq, 6000));
    CHECK(auto_seq_active_count(&seq) == 0u);
    CHECK(auto_seq_inactive_count(&seq) == 1u);

    event.rx_slot_id = 7;
    CHECK(auto_seq_on_addressed_rx(&seq, &event) == AUTO_SEQ_OK);
    CHECK(expect_head(&seq, "W6ABC", AUTO_SEQ_STATE_SIGNOFF,
                      AUTO_SEQ_MSG_TX5, 0u) == 0);
    CHECK(!auto_seq_prepare_log_event(&seq, &log_event));
    return 0;
}

static int test_fd_rogers_late_signoff_guard(void)
{
    AutoSeq seq;
    AutoSeqRxEvent event;
    AutoSeqLogEvent log_event;

    CHECK(auto_seq_init(&seq, NULL));
    CHECK(auto_seq_set_fd_exchange(&seq, "1B SCV"));

    event = event_for("N6HAN", AUTO_SEQ_MSG_TX1, 1);
    event.flags = AUTO_SEQ_RX_FLAG_CQ | AUTO_SEQ_RX_FLAG_FD;
    CHECK(auto_seq_on_manual_rx(&seq, &event) == AUTO_SEQ_OK);

    event = event_for("N6HAN", AUTO_SEQ_MSG_TX3, 3);
    event.flags = AUTO_SEQ_RX_FLAG_TO_ME | AUTO_SEQ_RX_FLAG_FD;
    snprintf(event.fd_exchange, sizeof(event.fd_exchange), "%s", "R 1A SCV");
    CHECK(auto_seq_on_addressed_rx(&seq, &event) == AUTO_SEQ_OK);
    CHECK(expect_head(&seq, "N6HAN", AUTO_SEQ_STATE_ROGERS,
                      AUTO_SEQ_MSG_TX4, 0u) == 0);

    CHECK(auto_seq_prepare_log_event(&seq, &log_event));
    CHECK(log_event.tx_kind == AUTO_SEQ_MSG_TX4);
    CHECK(auto_seq_ack_log_event(&seq, &log_event, true, true));

    event = event_for("N6HAN", AUTO_SEQ_MSG_TX4, 5);
    event.flags = AUTO_SEQ_RX_FLAG_TO_ME | AUTO_SEQ_RX_FLAG_FD;
    CHECK(auto_seq_on_addressed_rx(&seq, &event) == AUTO_SEQ_OK);
    CHECK(auto_seq_active_count(&seq) == 0u);

    /* Once terminal, another late RR73 cannot reincarnate a duplicate QSO. */
    event.rx_slot_id = 7;
    CHECK(auto_seq_on_addressed_rx(&seq, &event) == AUTO_SEQ_IGNORED);
    CHECK(auto_seq_total_count(&seq) == 0u);
    return 0;
}

int main(void)
{
    CHECK(test_normal_qso_progression() == 0);
    CHECK(test_report_deadlock_equivalence() == 0);
    CHECK(test_reactivation_deadlock_equivalence() == 0);
    CHECK(test_reincarnation_guards() == 0);
    CHECK(test_freetext_preemption_equivalence() == 0);
    CHECK(test_fd_signoff_reentry_idempotence() == 0);
    CHECK(test_fd_rogers_late_signoff_guard() == 0);

    printf("AS8 sizeof(QsoContext)=%zu sizeof(AutoSeq)=%zu\n",
           sizeof(QsoContext), sizeof(AutoSeq));
    puts("ft8_auto_seq_as8_test: PASS");
    return 0;
}

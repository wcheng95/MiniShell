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
                                int64_t slot_id)
{
    AutoSeqRxEvent event;
    memset(&event, 0, sizeof(event));
    snprintf(event.dxcall, sizeof(event.dxcall), "%s", dxcall);
    event.kind = kind;
    event.rx_slot_id = slot_id;
    event.offset_hz = 1500;
    event.snr_db = -15;
    event.report_db = AUTO_SEQ_SNR_UNKNOWN;
    return event;
}

static int test_config_and_sizes(void)
{
    AutoSeqConfig config = auto_seq_default_config();
    AutoSeq seq;

    CHECK(sizeof(QsoContext) <= 64u);
    CHECK(sizeof(AutoSeq) <= 2048u);
    CHECK(config.max_retry == AUTO_SEQ_DEFAULT_MAX_RETRY);
    snprintf(config.callsign, sizeof(config.callsign), "%s", "ag6aq");
    snprintf(config.grid, sizeof(config.grid), "%s", "cm97");
    config.skip_tx1 = 1u;
    CHECK(auto_seq_init(&seq, &config));
    CHECK(strcmp(seq.config.callsign, "AG6AQ") == 0);
    CHECK(strcmp(seq.config.grid, "CM97") == 0);
    CHECK(auto_seq_get_skip_tx1(&seq));
    CHECK(auto_seq_get_max_retry(&seq) == (int)AUTO_SEQ_DEFAULT_MAX_RETRY);
    CHECK(auto_seq_active_count(&seq) == 0u);
    CHECK(auto_seq_inactive_count(&seq) == 0u);

    auto_seq_clear(&seq);
    CHECK(strcmp(seq.config.callsign, "AG6AQ") == 0);
    CHECK(auto_seq_get_skip_tx1(&seq));

    CHECK(auto_seq_next_tx_for_state(AUTO_SEQ_STATE_CALLING) == AUTO_SEQ_MSG_NONE);
    CHECK(auto_seq_next_tx_for_state(AUTO_SEQ_STATE_REPLYING) == AUTO_SEQ_MSG_TX1);
    CHECK(auto_seq_next_tx_for_state(AUTO_SEQ_STATE_REPORT) == AUTO_SEQ_MSG_TX2);
    CHECK(auto_seq_next_tx_for_state(AUTO_SEQ_STATE_ROGER_REPORT) == AUTO_SEQ_MSG_TX3);
    CHECK(auto_seq_next_tx_for_state(AUTO_SEQ_STATE_ROGERS) == AUTO_SEQ_MSG_TX4);
    CHECK(auto_seq_next_tx_for_state(AUTO_SEQ_STATE_SIGNOFF) == AUTO_SEQ_MSG_TX5);
    CHECK(auto_seq_next_tx_for_state(AUTO_SEQ_STATE_IDLE) == AUTO_SEQ_MSG_NONE);
    return 0;
}

static int test_manual_start_and_skip_tx1(void)
{
    AutoSeq seq;
    AutoSeqRxEvent event = event_for("w1xyz", AUTO_SEQ_MSG_TX1, 77);
    QsoContext ctx;

    snprintf(event.dxgrid, sizeof(event.dxgrid), "%s", "fn42");
    event.flags = AUTO_SEQ_RX_FLAG_CQ;
    event.snr_db = -17;
    event.offset_hz = 1425;

    CHECK(auto_seq_init(&seq, NULL));
    CHECK(auto_seq_on_manual_rx(&seq, &event) == AUTO_SEQ_OK);
    CHECK(auto_seq_active_count(&seq) == 1u);
    CHECK(auto_seq_get_active_context(&seq, 0u, &ctx));
    CHECK(strcmp(ctx.dxcall, "W1XYZ") == 0);
    CHECK(strcmp(ctx.dxgrid, "FN42") == 0);
    CHECK(ctx.state == AUTO_SEQ_STATE_REPLYING);
    CHECK(auto_seq_next_tx_for_state(ctx.state) == AUTO_SEQ_MSG_TX1);
    CHECK(ctx.snr_tx == -17);
    CHECK(ctx.offset_hz == 1425);
    CHECK(ctx.tx_parity == 0u);

    CHECK(auto_seq_init(&seq, NULL));
    auto_seq_set_skip_tx1(&seq, true);
    CHECK(auto_seq_on_manual_rx(&seq, &event) == AUTO_SEQ_OK);
    CHECK(auto_seq_get_active_context(&seq, 0u, &ctx));
    CHECK(ctx.state == AUTO_SEQ_STATE_REPORT);
    CHECK(auto_seq_next_tx_for_state(ctx.state) == AUTO_SEQ_MSG_TX2);
    return 0;
}

static int test_addressed_progression(void)
{
    AutoSeq seq;
    AutoSeqRxEvent cq = event_for("W1XYZ", AUTO_SEQ_MSG_TX1, 100);
    AutoSeqRxEvent reply;
    QsoContext ctx;

    snprintf(cq.dxgrid, sizeof(cq.dxgrid), "%s", "FN42");
    cq.flags = AUTO_SEQ_RX_FLAG_CQ;
    cq.snr_db = -18;

    CHECK(auto_seq_init(&seq, NULL));
    CHECK(auto_seq_on_manual_rx(&seq, &cq) == AUTO_SEQ_OK);

    reply = event_for("W1XYZ", AUTO_SEQ_MSG_TX2, 101);
    reply.flags = AUTO_SEQ_RX_FLAG_TO_ME;
    reply.report_db = -10;
    CHECK(auto_seq_on_addressed_rx(&seq, &reply) == AUTO_SEQ_OK);
    CHECK(auto_seq_get_active_context(&seq, 0u, &ctx));
    CHECK(ctx.state == AUTO_SEQ_STATE_ROGER_REPORT);
    CHECK(auto_seq_next_tx_for_state(ctx.state) == AUTO_SEQ_MSG_TX3);
    CHECK(ctx.snr_tx == -18);
    CHECK(ctx.snr_rx == -10);
    CHECK(ctx.tx_parity == 0u);

    reply.kind = AUTO_SEQ_MSG_TX4;
    reply.report_db = AUTO_SEQ_SNR_UNKNOWN;
    reply.rx_slot_id = 103;
    CHECK(auto_seq_on_addressed_rx(&seq, &reply) == AUTO_SEQ_OK);
    CHECK(auto_seq_get_active_context(&seq, 0u, &ctx));
    CHECK(ctx.state == AUTO_SEQ_STATE_SIGNOFF);
    CHECK((ctx.flags & AUTO_SEQ_FLAG_PARK_AFTER_SIGNOFF) != 0u);
    CHECK(auto_seq_next_tx_for_state(ctx.state) == AUTO_SEQ_MSG_TX5);

    reply.kind = AUTO_SEQ_MSG_TX5;
    CHECK(auto_seq_on_addressed_rx(&seq, &reply) == AUTO_SEQ_OK);
    CHECK(auto_seq_active_count(&seq) == 0u);
    return 0;
}

static int test_unknown_mid_qso_guard(void)
{
    AutoSeq seq;
    AutoSeqRxEvent event = event_for("K9XYZ", AUTO_SEQ_MSG_TX3, 200);
    QsoContext ctx;

    event.flags = AUTO_SEQ_RX_FLAG_TO_ME;
    event.report_db = -8;
    CHECK(auto_seq_init(&seq, NULL));
    CHECK(auto_seq_on_addressed_rx(&seq, &event) == AUTO_SEQ_IGNORED);
    CHECK(auto_seq_active_count(&seq) == 0u);

    event.kind = AUTO_SEQ_MSG_TX4;
    CHECK(auto_seq_on_addressed_rx(&seq, &event) == AUTO_SEQ_IGNORED);
    event.kind = AUTO_SEQ_MSG_TX5;
    CHECK(auto_seq_on_addressed_rx(&seq, &event) == AUTO_SEQ_IGNORED);

    event.kind = AUTO_SEQ_MSG_TX2;
    event.snr_db = -12;
    event.report_db = -7;
    CHECK(auto_seq_on_addressed_rx(&seq, &event) == AUTO_SEQ_OK);
    CHECK(auto_seq_get_active_context(&seq, 0u, &ctx));
    CHECK(ctx.state == AUTO_SEQ_STATE_ROGER_REPORT);
    CHECK(ctx.snr_tx == -12);
    CHECK(ctx.snr_rx == -7);
    return 0;
}

static int test_retry_inactive_and_reactivation(void)
{
    AutoSeq seq;
    AutoSeqRxEvent event = event_for("N6ABC", AUTO_SEQ_MSG_TX1, 300);
    QsoContext ctx;

    CHECK(auto_seq_init(&seq, NULL));
    auto_seq_set_skip_tx1(&seq, true);
    auto_seq_set_max_retry(&seq, 1);
    event.snr_db = -11;
    CHECK(auto_seq_on_manual_rx(&seq, &event) == AUTO_SEQ_OK);
    CHECK(auto_seq_get_active_context(&seq, 0u, &ctx));
    CHECK(ctx.state == AUTO_SEQ_STATE_REPORT);

    CHECK(auto_seq_tick(&seq, 1000));
    CHECK(auto_seq_get_active_context(&seq, 0u, &ctx));
    CHECK(ctx.retry_counter == 1u);
    CHECK(auto_seq_tick(&seq, 2000));
    CHECK(auto_seq_active_count(&seq) == 0u);
    CHECK(auto_seq_inactive_count(&seq) == 1u);
    CHECK(auto_seq_get_inactive_context(&seq, 0u, &ctx));
    CHECK(ctx.state == AUTO_SEQ_STATE_REPORT);
    CHECK(ctx.snr_tx == -11);
    CHECK(ctx.inactive_since_ms == 2000);

    event.kind = AUTO_SEQ_MSG_TX2;
    event.flags = AUTO_SEQ_RX_FLAG_TO_ME;
    event.report_db = -6;
    event.rx_slot_id = 305;
    CHECK(auto_seq_on_addressed_rx(&seq, &event) == AUTO_SEQ_OK);
    CHECK(auto_seq_active_count(&seq) == 1u);
    CHECK(auto_seq_inactive_count(&seq) == 0u);
    CHECK(auto_seq_get_active_context(&seq, 0u, &ctx));
    CHECK(ctx.state == AUTO_SEQ_STATE_ROGER_REPORT);
    CHECK(ctx.snr_tx == -11);
    CHECK(ctx.snr_rx == -6);
    CHECK(ctx.retry_counter == 0u);
    CHECK(ctx.inactive_since_ms == 0);

    CHECK(auto_seq_init(&seq, NULL));
    auto_seq_set_max_retry(&seq, 0);
    event = event_for("N7XYZ", AUTO_SEQ_MSG_TX1, 310);
    CHECK(auto_seq_on_manual_rx(&seq, &event) == AUTO_SEQ_OK);
    CHECK(auto_seq_tick(&seq, 3000));
    CHECK(auto_seq_total_count(&seq) == 0u);
    return 0;
}

static int test_priority_rotation_drop_and_views(void)
{
    AutoSeq seq;
    AutoSeqRxEvent a = event_for("A1AAA", AUTO_SEQ_MSG_TX1, 400);
    AutoSeqRxEvent b = event_for("B2BBB", AUTO_SEQ_MSG_TX1, 402);
    AutoSeqRxEvent advance;
    QsoContext ctx;
    AutoSeqQsoView views[2];

    CHECK(auto_seq_init(&seq, NULL));
    CHECK(auto_seq_on_manual_rx(&seq, &a) == AUTO_SEQ_OK);
    CHECK(auto_seq_on_manual_rx(&seq, &b) == AUTO_SEQ_OK);
    CHECK(auto_seq_get_active_context(&seq, 0u, &ctx));
    CHECK(strcmp(ctx.dxcall, "A1AAA") == 0);

    CHECK(auto_seq_rotate_same_parity(&seq));
    CHECK(auto_seq_get_active_context(&seq, 0u, &ctx));
    CHECK(strcmp(ctx.dxcall, "B2BBB") == 0);

    advance = event_for("A1AAA", AUTO_SEQ_MSG_TX2, 403);
    advance.flags = AUTO_SEQ_RX_FLAG_TO_ME;
    advance.report_db = -5;
    CHECK(auto_seq_on_addressed_rx(&seq, &advance) == AUTO_SEQ_OK);
    CHECK(auto_seq_get_active_context(&seq, 0u, &ctx));
    CHECK(strcmp(ctx.dxcall, "A1AAA") == 0);
    CHECK(ctx.state == AUTO_SEQ_STATE_ROGER_REPORT);

    CHECK(auto_seq_snapshot_active(&seq, views, 2u) == 2u);
    CHECK(views[0].active == 1u);
    CHECK(views[0].next_tx == AUTO_SEQ_MSG_TX3);

    CHECK(auto_seq_drop_index(&seq, 0u, 5000));
    CHECK(auto_seq_active_count(&seq) == 1u);
    CHECK(auto_seq_inactive_count(&seq) == 1u);
    CHECK(auto_seq_snapshot_inactive(&seq, views, 2u) == 1u);
    CHECK(views[0].active == 0u);
    CHECK(strcmp(views[0].dxcall, "A1AAA") == 0);
    return 0;
}

static int test_retry_config_update(void)
{
    AutoSeq seq;
    AutoSeqRxEvent event = event_for("W6ABC", AUTO_SEQ_MSG_TX1, 500);
    QsoContext ctx;

    CHECK(auto_seq_init(&seq, NULL));
    CHECK(auto_seq_on_manual_rx(&seq, &event) == AUTO_SEQ_OK);
    auto_seq_set_max_retry(&seq, 2);
    CHECK(auto_seq_get_active_context(&seq, 0u, &ctx));
    CHECK(ctx.retry_limit == 2u);
    CHECK(auto_seq_tick(&seq, 6000));
    CHECK(auto_seq_tick(&seq, 7000));
    auto_seq_set_max_retry(&seq, 1);
    CHECK(auto_seq_get_active_context(&seq, 0u, &ctx));
    CHECK(ctx.retry_limit == 1u);
    CHECK(ctx.retry_counter == 1u);
    return 0;
}

static int test_capacity_and_oldest_inactive_eviction(void)
{
    AutoSeq seq;
    AutoSeqRxEvent event;
    QsoContext ctx;
    char call[AUTO_SEQ_CALL_CAP];
    bool found_oldest = false;
    size_t i;

    CHECK(auto_seq_init(&seq, NULL));
    auto_seq_set_skip_tx1(&seq, true);
    auto_seq_set_max_retry(&seq, 0);

    for (i = 0u; i < AUTO_SEQ_MAX_QUEUE; ++i) {
        snprintf(call, sizeof(call), "W%02uAAA", (unsigned)i);
        event = event_for(call, AUTO_SEQ_MSG_TX1, 600 + (int64_t)i * 2);
        CHECK(auto_seq_on_manual_rx(&seq, &event) == AUTO_SEQ_OK);
        CHECK(auto_seq_tick(&seq, 10000 + (int64_t)i));
    }
    CHECK(auto_seq_active_count(&seq) == 0u);
    CHECK(auto_seq_inactive_count(&seq) == AUTO_SEQ_MAX_QUEUE);

    event = event_for("NEW1", AUTO_SEQ_MSG_TX1, 700);
    CHECK(auto_seq_on_manual_rx(&seq, &event) == AUTO_SEQ_OK);
    CHECK(auto_seq_active_count(&seq) == 1u);
    CHECK(auto_seq_inactive_count(&seq) == AUTO_SEQ_MAX_QUEUE - 1u);
    CHECK(auto_seq_total_count(&seq) == AUTO_SEQ_MAX_QUEUE);

    for (i = 0u; i < auto_seq_inactive_count(&seq); ++i) {
        CHECK(auto_seq_get_inactive_context(&seq, i, &ctx));
        if (strcmp(ctx.dxcall, "W00AAA") == 0) found_oldest = true;
    }
    CHECK(!found_oldest);
    return 0;
}

int main(void)
{
    CHECK(test_config_and_sizes() == 0);
    CHECK(test_manual_start_and_skip_tx1() == 0);
    CHECK(test_addressed_progression() == 0);
    CHECK(test_unknown_mid_qso_guard() == 0);
    CHECK(test_retry_inactive_and_reactivation() == 0);
    CHECK(test_priority_rotation_drop_and_views() == 0);
    CHECK(test_retry_config_update() == 0);
    CHECK(test_capacity_and_oldest_inactive_eviction() == 0);

    printf("AS2 sizeof(QsoContext)=%zu sizeof(AutoSeq)=%zu\n",
           sizeof(QsoContext), sizeof(AutoSeq));
    puts("ft8_auto_seq_as2_test: PASS");
    return 0;
}

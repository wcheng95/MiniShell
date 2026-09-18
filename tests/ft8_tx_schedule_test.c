#include "app_tx_schedule.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    const unsigned polls[] = {1, 5, 10, 20};
    for (unsigned p = 0; p < 4; ++p) {
        AppTxSchedule schedule;
        uint64_t anchor = 100000000;
        assert(app_tx_schedule_begin(&schedule, anchor + 33000, 33));
        assert(schedule.slot_start_us == anchor);
        unsigned sent = 0;
        for (uint64_t now = anchor + 33000;; now += polls[p] * 1000u) {
            uint8_t tone = 255;
            AppTxDue due = app_tx_schedule_poll(&schedule, now, &tone);
            if (due == APP_TX_DONE) { assert(now - anchor >= 12640000); break; }
            if (due == APP_TX_WAIT) continue;
            assert(due == APP_TX_TONE && tone == sent);
            uint64_t late = now - anchor - tone * 160000u;
            assert(sent == 0 || late < polls[p] * 1000u);
            ++sent;
        }
        assert(sent == 79);
    }
    AppTxSchedule schedule;
    uint8_t tone;
    assert(app_tx_schedule_begin(&schedule, 1000499000, 499));
    assert(app_tx_schedule_poll(&schedule, 1000499000, &tone) == APP_TX_TONE && tone == 3);
    assert(app_tx_schedule_poll(&schedule, 1000500000, &tone) == APP_TX_WAIT);
    assert(app_tx_schedule_poll(&schedule, 1001999000, &tone) == APP_TX_TONE && tone == 12);
    assert(app_tx_schedule_poll(&schedule, 1001999000, &tone) == APP_TX_WAIT);
    assert(app_tx_schedule_poll(&schedule, 1012640000, &tone) == APP_TX_DONE);
    assert(app_tx_schedule_poll(&schedule, 1000000000, &tone) == APP_TX_CLOCK_ERROR);
    assert(!app_tx_schedule_begin(&schedule, 100, 499));
    assert(!app_tx_schedule_begin(&schedule, UINT64_MAX, 12640));
    assert(app_tx_schedule_begin(&schedule, UINT64_MAX - 1, 0));
    assert(app_tx_schedule_poll(&schedule, UINT64_MAX, &tone) == APP_TX_TONE && tone == 0);
    puts("absolute FT8 timing: poll bounds, late entry, stalls, end, clock/overflow checks PASS");
    return 0;
}

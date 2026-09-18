#include "app_tx_schedule.h"
#include "tx_encoder.h"

#define SYMBOL_US ((uint64_t)FT8_TX_SYMBOL_PERIOD_MS * 1000u)

bool app_tx_schedule_begin(AppTxSchedule *schedule, uint64_t now_us, uint16_t ms_into_slot)
{
    uint64_t elapsed = (uint64_t)ms_into_slot * 1000u;
    if (!schedule || elapsed >= FT8_TX_TONE_COUNT * SYMBOL_US || now_us < elapsed) return false;
    schedule->slot_start_us = now_us - elapsed;
    schedule->last_poll_us = now_us;
    schedule->next_tone = (uint8_t)(elapsed / SYMBOL_US);
    return true;
}

AppTxDue app_tx_schedule_poll(AppTxSchedule *schedule, uint64_t now_us, uint8_t *tone)
{
    if (!schedule || !tone || now_us < schedule->last_poll_us || now_us < schedule->slot_start_us)
        return APP_TX_CLOCK_ERROR;
    schedule->last_poll_us = now_us;
    uint64_t elapsed = now_us - schedule->slot_start_us;
    if (elapsed >= FT8_TX_TONE_COUNT * SYMBOL_US) return APP_TX_DONE;
    uint8_t current = (uint8_t)(elapsed / SYMBOL_US);
    if (current < schedule->next_tone) return APP_TX_WAIT;
    *tone = current;
    schedule->next_tone = current + 1u;
    return APP_TX_TONE;
}

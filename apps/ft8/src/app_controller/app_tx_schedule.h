#ifndef APP_TX_SCHEDULE_H
#define APP_TX_SCHEDULE_H
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint64_t slot_start_us;
    uint64_t last_poll_us;
    uint8_t next_tone;
} AppTxSchedule;
typedef enum { APP_TX_WAIT, APP_TX_TONE, APP_TX_DONE, APP_TX_CLOCK_ERROR } AppTxDue;

/* Pure private timing math; caller supplies all observations. */
bool app_tx_schedule_begin(AppTxSchedule *schedule, uint64_t now_us, uint16_t ms_into_slot);
AppTxDue app_tx_schedule_poll(AppTxSchedule *schedule, uint64_t now_us, uint8_t *tone);
#endif

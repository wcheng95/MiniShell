#pragma once
#include "keyer_types.h"

#define KEYER_TX_CAPACITY 511u

typedef enum { TX_OK, TX_FULL, TX_UNSUPPORTED } tx_result_t;
typedef enum { TX_IDLE, TX_ELEMENT, TX_ELEMENT_GAP, TX_CHAR_GAP, TX_WORD_GAP } tx_phase_t;
typedef struct {
    char fifo[KEYER_TX_CAPACITY + 1];
    unsigned count;
    const char *pattern;
    unsigned element;
    tx_phase_t phase;
    uint64_t due_us, typed_us, repeat_due_us, tune_start_us;
    bool immediate, repeat, repeat_waiting, tune, down;
} tx_engine_t;

void tx_engine_init(tx_engine_t *tx);
void tx_engine_cancel(tx_engine_t *tx);
tx_result_t tx_engine_append(tx_engine_t *tx, const char *text, uint64_t now);
tx_result_t tx_engine_memory(tx_engine_t *tx, const keyer_config_t *config,
                             unsigned index, uint64_t now);
void tx_engine_backspace(tx_engine_t *tx);
void tx_engine_start(tx_engine_t *tx);
void tx_engine_tune(tx_engine_t *tx, uint64_t now);
/* Physical input preempts every automatic state before any TX step. */
bool tx_engine_step(tx_engine_t *tx, const keyer_config_t *config,
                    uint64_t now, bool physical);

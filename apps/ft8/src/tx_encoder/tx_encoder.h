#ifndef FT8_TX_ENCODER_H
#define FT8_TX_ENCODER_H

#include "auto_seq_tx_intent.h"

#define FT8_TX_TONE_COUNT 79u
#define FT8_TX_SYMBOL_PERIOD_MS 160u
#define FT8_TX_TONE_SPACING_HZ 6.25f

typedef struct {
    char canonical_text[64];
    uint8_t payload[10];
    uint8_t tones[FT8_TX_TONE_COUNT];
    int16_t base_hz;
    uint8_t tx_parity;
    bool valid;
} Ft8TxPlan;

typedef enum {
    FT8_TX_ENCODE_OK = 0,
    FT8_TX_ENCODE_INVALID = -1,
    FT8_TX_ENCODE_UNSUPPORTED = -2
} Ft8TxEncodeStatus;

/* Caller-owned immutable snapshot after success; no retained intent pointers,
 * heap, clock, or I/O. Failure clears out_plan, including its valid flag.
 * Parity must be 0/1. Offset is copied verbatim; RF range policy belongs to caller. */
Ft8TxEncodeStatus ft8_tx_encode(const AutoSeqTxIntent *intent, Ft8TxPlan *out_plan);

/* Pure mapping for a validated plan's tone index (0..7). */
float ft8_tx_tone_hz(int16_t base_hz, uint8_t tone_index);
#endif

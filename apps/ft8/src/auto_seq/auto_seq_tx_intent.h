#ifndef FT8_AUTO_SEQ_TX_INTENT_H
#define FT8_AUTO_SEQ_TX_INTENT_H

#include <stdbool.h>
#include <stdint.h>

#include "auto_seq.h"

typedef uint8_t AutoSeqTxIntentType;
#define AUTO_SEQ_TX_INTENT_NONE     ((AutoSeqTxIntentType)0u)
#define AUTO_SEQ_TX_INTENT_QSO      ((AutoSeqTxIntentType)1u)
#define AUTO_SEQ_TX_INTENT_CQ       ((AutoSeqTxIntentType)2u)
#define AUTO_SEQ_TX_INTENT_FREETEXT ((AutoSeqTxIntentType)3u)

#define AUTO_SEQ_TX_INTENT_FLAG_FD 0x01u

/*
 * Pure semantic request from AutoSeq to app_controller.
 *
 * This is intentionally not an encoded FT8 frame and contains no platform,
 * Audio, CAT, or radio operation. It is a caller-owned snapshot of what the
 * current queue head wants transmitted.
 */
typedef struct {
    AutoSeqTxIntentType type;
    AutoSeqMessageKind message_kind;
    AutoSeqCqType cq_type;
    uint8_t tx_parity;
    uint8_t flags;
    int16_t offset_hz;
    int8_t report_db;
    uint16_t retry_counter;
    uint16_t retry_limit;
    char callsign[AUTO_SEQ_CALL_CAP];
    char grid[AUTO_SEQ_GRID_CAP];
    char dxcall[AUTO_SEQ_CALL_CAP];
    char dxgrid[AUTO_SEQ_GRID_CAP];
    char fd_exchange[AUTO_SEQ_FD_EXCHANGE_CAP];
    char text[AUTO_SEQ_FREETEXT_CAP];
} AutoSeqTxIntent;

bool auto_seq_prepare_tx_intent(const AutoSeq *seq, AutoSeqTxIntent *out_intent);

#endif

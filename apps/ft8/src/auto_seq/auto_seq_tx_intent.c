#include "auto_seq_tx_intent.h"

#include <string.h>

static void copy_field(char *out, size_t out_size, const char *text)
{
    size_t length;
    if (out == NULL || out_size == 0u || text == NULL) return;
    length = strlen(text);
    if (length >= out_size) length = out_size - 1u;
    memcpy(out, text, length);
    out[length] = '\0';
}

bool auto_seq_prepare_tx_intent(const AutoSeq *seq, AutoSeqTxIntent *out_intent)
{
    const QsoContext *ctx;
    AutoSeqMessageKind next_tx;

    if (seq == NULL || out_intent == NULL || seq->active_count == 0u) return false;

    ctx = &seq->queue[0];
    memset(out_intent, 0, sizeof(*out_intent));
    out_intent->tx_parity = ctx->tx_parity & 1u;
    out_intent->offset_hz = ctx->offset_hz;
    out_intent->report_db = ctx->snr_tx;
    out_intent->retry_counter = ctx->retry_counter;
    out_intent->retry_limit = ctx->retry_limit;
    out_intent->cq_type = seq->config.cq_type;

    copy_field(out_intent->callsign, sizeof(out_intent->callsign), seq->config.callsign);
    copy_field(out_intent->grid, sizeof(out_intent->grid), seq->config.grid);
    copy_field(out_intent->dxcall, sizeof(out_intent->dxcall), ctx->dxcall);
    copy_field(out_intent->dxgrid, sizeof(out_intent->dxgrid), ctx->dxgrid);

    if ((ctx->flags & AUTO_SEQ_FLAG_FD) != 0u) {
        out_intent->flags |= AUTO_SEQ_TX_INTENT_FLAG_FD;
        copy_field(out_intent->fd_exchange, sizeof(out_intent->fd_exchange),
                   seq->config.fd_exchange);
    }

    if (ctx->state == AUTO_SEQ_STATE_CALLING) {
        if ((ctx->flags & AUTO_SEQ_FLAG_FREETEXT) != 0u) {
            if (seq->pending_freetext[0] == '\0') return false;
            out_intent->type = AUTO_SEQ_TX_INTENT_FREETEXT;
            copy_field(out_intent->text, sizeof(out_intent->text), seq->pending_freetext);
            return true;
        }

        out_intent->type = AUTO_SEQ_TX_INTENT_CQ;
        out_intent->message_kind = AUTO_SEQ_MSG_TX6;
        if (seq->config.cq_type == AUTO_SEQ_CQ_FREETEXT) {
            if (seq->config.cq_freetext[0] == '\0') return false;
            copy_field(out_intent->text, sizeof(out_intent->text), seq->config.cq_freetext);
        }
        if (seq->config.cq_type == AUTO_SEQ_CQ_FD) {
            out_intent->flags |= AUTO_SEQ_TX_INTENT_FLAG_FD;
            copy_field(out_intent->fd_exchange, sizeof(out_intent->fd_exchange),
                       seq->config.fd_exchange);
        }
        return true;
    }

    next_tx = auto_seq_next_tx_for_state(ctx->state);
    if (next_tx < AUTO_SEQ_MSG_TX1 || next_tx > AUTO_SEQ_MSG_TX5) return false;

    out_intent->type = AUTO_SEQ_TX_INTENT_QSO;
    out_intent->message_kind = next_tx;
    return true;
}

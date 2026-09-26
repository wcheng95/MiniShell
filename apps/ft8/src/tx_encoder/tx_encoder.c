#include "tx_encoder.h"
#include "tx_channel.h"
#include "ft8_message_codec.h"
#include <stdio.h>
#include <string.h>

static bool whitespace(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

static bool normalize(char *out, size_t capacity, const char *in, size_t input_capacity)
{
    if (!memchr(in, '\0', input_capacity)) return false;
    size_t used = 0;
    bool space = false;
    for (size_t i = 0; in[i]; ++i) {
        char c = in[i];
        if (whitespace(c)) { space = used != 0; continue; }
        if (space) {
            if (used + 1 >= capacity) return false;
            out[used++] = ' ';
            space = false;
        }
        if (used + 1 >= capacity) return false;
        out[used++] = c >= 'a' && c <= 'z' ? (char)(c - 'a' + 'A') : c;
    }
    out[used] = '\0';
    return used != 0;
}

static bool grid_field(const AutoSeqTxIntent *intent, Ft8ProtocolStandard *standard)
{
    char grid[AUTO_SEQ_GRID_CAP];
    if (!normalize(grid, sizeof(grid), intent->grid, sizeof(intent->grid))) return false;
    size_t length = strlen(grid);
    if (length != 4 && length != 6) return false;
    if (length == 6 && (grid[4] < 'A' || grid[4] > 'X' || grid[5] < 'A' || grid[5] > 'X')) return false;
    memcpy(standard->extra, grid, 4);
    standard->extra[4] = '\0';
    standard->extra_kind = FT8_PROTOCOL_FIELD_GRID;
    return true;
}

static bool fd_fields(const AutoSeqTxIntent *intent, Ft8ProtocolArrlFd *fd)
{
    char exchange[AUTO_SEQ_FD_EXCHANGE_CAP];
    if (!normalize(fd->call_to, sizeof(fd->call_to), intent->dxcall, sizeof(intent->dxcall)) ||
        !normalize(fd->call_de, sizeof(fd->call_de), intent->callsign, sizeof(intent->callsign)) ||
        !normalize(exchange, sizeof(exchange), intent->fd_exchange, sizeof(intent->fd_exchange))) return false;
    const char *p = exchange;
    unsigned count = 0, digits = 0;
    while (*p >= '0' && *p <= '9' && digits < 2) {
        count = count * 10 + (unsigned)(*p++ - '0'); ++digits;
    }
    if (!digits || count < 1 || count > 32 || *p < 'A' || *p > 'F') return false;
    fd->class_letter = *p++;
    if (*p++ != ' ') return false;
    size_t length = strlen(p);
    if (!length || length >= sizeof(fd->section)) return false;
    memcpy(fd->section, p, length + 1);
    fd->transmitter_count = (uint8_t)count;
    fd->has_r = intent->message_kind == AUTO_SEQ_MSG_TX3;
    return true;
}

static bool project(const AutoSeqTxIntent *intent, Ft8ProtocolMessage *message)
{
    if (intent->type == AUTO_SEQ_TX_INTENT_FREETEXT ||
        (intent->type == AUTO_SEQ_TX_INTENT_CQ && intent->cq_type == AUTO_SEQ_CQ_FREETEXT)) {
        message->type = FT8_PROTOCOL_FREE_TEXT;
        return normalize(message->data.free_text.text, sizeof(message->data.free_text.text),
                         intent->text, sizeof(intent->text));
    }
    if (intent->type == AUTO_SEQ_TX_INTENT_QSO && (intent->flags & AUTO_SEQ_TX_INTENT_FLAG_FD) &&
        (intent->message_kind == AUTO_SEQ_MSG_TX2 || intent->message_kind == AUTO_SEQ_MSG_TX3)) {
        message->type = FT8_PROTOCOL_ARRL_FD;
        return fd_fields(intent, &message->data.arrl_fd);
    }
    message->type = FT8_PROTOCOL_STANDARD;
    Ft8ProtocolStandard *standard = &message->data.standard;
    if (!normalize(standard->call_de, sizeof(standard->call_de), intent->callsign, sizeof(intent->callsign)))
        return false;
    if (intent->type == AUTO_SEQ_TX_INTENT_CQ) {
        static const char *const cq[] = {"CQ", "CQ SOTA", "CQ POTA", "CQ QRP", "CQ FD"};
        if (intent->cq_type == AUTO_SEQ_CQ_MODIFIER) {
            if (!memchr(intent->cq_modifier, '\0', sizeof(intent->cq_modifier)) ||
                !intent->cq_modifier[0]) return false;
            snprintf(standard->call_to, sizeof(standard->call_to), "CQ %s", intent->cq_modifier);
        } else {
            if (intent->cq_type >= sizeof(cq) / sizeof(cq[0])) return false;
            strcpy(standard->call_to, cq[intent->cq_type]);
        }
        return grid_field(intent, standard);
    }
    if (intent->type != AUTO_SEQ_TX_INTENT_QSO ||
        !normalize(standard->call_to, sizeof(standard->call_to), intent->dxcall, sizeof(intent->dxcall))) return false;
    /* A QSO destination is a station, never a CQ token. */
    if (strncmp(standard->call_to, "CQ", 2) == 0 &&
        (standard->call_to[2] == '\0' || standard->call_to[2] == ' ')) return false;
    switch (intent->message_kind) {
    case AUTO_SEQ_MSG_TX1:
        return grid_field(intent, standard);
    case AUTO_SEQ_MSG_TX2:
    case AUTO_SEQ_MSG_TX3:
        standard->extra_kind = FT8_PROTOCOL_FIELD_REPORT;
        (void)snprintf(standard->extra, sizeof(standard->extra), "%s%+d",
                      intent->message_kind == AUTO_SEQ_MSG_TX3 ? "R" : "", intent->report_db);
        return true;
    case AUTO_SEQ_MSG_TX4:
    case AUTO_SEQ_MSG_TX5:
        standard->extra_kind = FT8_PROTOCOL_FIELD_TOKEN;
        strcpy(standard->extra, intent->message_kind == AUTO_SEQ_MSG_TX4 ? "RR73" : "73");
        return true;
    default:
        return false;
    }
}

Ft8TxEncodeStatus ft8_tx_encode(const AutoSeqTxIntent *intent, Ft8TxPlan *out_plan)
{
    if (!out_plan) return FT8_TX_ENCODE_INVALID;
    memset(out_plan, 0, sizeof(*out_plan));
    if (!intent || intent->tx_parity > 1 || (intent->flags & ~AUTO_SEQ_TX_INTENT_FLAG_FD))
        return FT8_TX_ENCODE_INVALID;
    Ft8ProtocolMessage message = {0};
    if (!project(intent, &message)) return FT8_TX_ENCODE_INVALID;
    Ft8TxPlan plan = {0};
    Ft8ProtocolCodecStatus status = ft8_protocol_encode(&message, plan.payload);
    if (status == FT8_PROTOCOL_CODEC_UNSUPPORTED) return FT8_TX_ENCODE_UNSUPPORTED;
    if (status != FT8_PROTOCOL_CODEC_OK) return FT8_TX_ENCODE_INVALID;
    Ft8DecodedPayload decoded = {0};
    memcpy(decoded.payload, plan.payload, sizeof(plan.payload));
    Ft8ProtocolMessage check = {0};
    if (ft8_protocol_decode(&decoded, NULL, &check) != FT8_PROTOCOL_CODEC_OK ||
        check.type != message.type) return FT8_TX_ENCODE_INVALID;
    if (intent->type == AUTO_SEQ_TX_INTENT_CQ && message.type == FT8_PROTOCOL_STANDARD &&
        check.has_unresolved_hash) {
        if (intent->cq_type != AUTO_SEQ_CQ) return FT8_TX_ENCODE_UNSUPPORTED;
        char call[FT8_PROTOCOL_CALL_CAP];
        strcpy(call, message.data.standard.call_de);
        memset(&message, 0, sizeof(message));
        message.type = FT8_PROTOCOL_NONSTD_CALL;
        message.data.nonstandard.is_cq = true;
        strcpy(message.data.nonstandard.call_to, "CQ");
        strcpy(message.data.nonstandard.call_de, call);
        if (ft8_protocol_encode(&message, plan.payload) != FT8_PROTOCOL_CODEC_OK)
            return FT8_TX_ENCODE_INVALID;
        memcpy(decoded.payload, plan.payload, sizeof(plan.payload));
        if (ft8_protocol_decode(&decoded, NULL, &check) != FT8_PROTOCOL_CODEC_OK ||
            check.type != message.type) return FT8_TX_ENCODE_INVALID;
    }
    if (check.has_unresolved_hash) {
        char *to, *de;
        const char *known_to, *known_de;
        if (message.type == FT8_PROTOCOL_STANDARD) {
            to = check.data.standard.call_to; de = check.data.standard.call_de;
            known_to = message.data.standard.call_to; known_de = message.data.standard.call_de;
        } else if (message.type == FT8_PROTOCOL_ARRL_FD) {
            to = check.data.arrl_fd.call_to; de = check.data.arrl_fd.call_de;
            known_to = message.data.arrl_fd.call_to; known_de = message.data.arrl_fd.call_de;
        } else return FT8_TX_ENCODE_INVALID;
        /* Retain decoder-normalized extra fields, replacing only known TX hashes.
         * Repacking below verifies those full calls reproduce the exact payload. */
        const char *extra = check.canonical_text + strlen(to) + 1 + strlen(de);
        int n = snprintf(plan.canonical_text, sizeof(plan.canonical_text), "%s %s%s",
                         known_to, known_de, extra);
        if (n < 0 || (size_t)n >= sizeof(plan.canonical_text)) return FT8_TX_ENCODE_INVALID;
        strcpy(to, known_to);
        strcpy(de, known_de);
    } else memcpy(plan.canonical_text, check.canonical_text, sizeof(plan.canonical_text));
    uint8_t verified[FT8_PAYLOAD_BYTES];
    if (ft8_protocol_encode(&check, verified) != FT8_PROTOCOL_CODEC_OK ||
        memcmp(verified, plan.payload, sizeof(verified)) != 0) return FT8_TX_ENCODE_INVALID;
    ft8_tx_channel_encode(plan.payload, plan.tones);
    plan.base_hz = intent->offset_hz;
    plan.tx_parity = intent->tx_parity;
    plan.valid = true;
    *out_plan = plan;
    return FT8_TX_ENCODE_OK;
}

float ft8_tx_tone_hz(int16_t base_hz, uint8_t tone_index)
{
    return (float)base_hz + (float)tone_index * FT8_TX_TONE_SPACING_HZ;
}

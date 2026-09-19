#include "tx_encoder.h"
#include "tx_channel.h"
#include "ft8_message_codec.h"
#include "ft8_crc.h"
#include "ft8_ldpc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%d: %s\n", __LINE__, #x); exit(1); } } while (0)
#include "ft8_tx_vectors/vectors.h"

static AutoSeqTxIntent qso(unsigned kind)
{
    AutoSeqTxIntent intent = {.type = AUTO_SEQ_TX_INTENT_QSO,
        .message_kind = (AutoSeqMessageKind)kind, .tx_parity = 1, .offset_hz = 1500,
        .report_db = -12};
    strcpy(intent.callsign, "K9XYZ");
    strcpy(intent.dxcall, "W1ABC");
    strcpy(intent.grid, "FN42");
    return intent;
}

static AutoSeqTxIntent vector_intent(unsigned i)
{
    AutoSeqTxIntent intent = qso(AUTO_SEQ_MSG_TX1);
    if (i == 0 || (i >= 6 && i <= 9) || i == 17 || i == 18) {
        intent.type = AUTO_SEQ_TX_INTENT_CQ;
        intent.message_kind = AUTO_SEQ_MSG_TX6;
        intent.cq_type = i >= 6 && i <= 9 ? (AutoSeqCqType)(i - 5) : AUTO_SEQ_CQ;
        strcpy(intent.callsign, i == 17 ? "3DA0XYZ" : i == 18 ? "3XA0XYZ" : "W1XYZ");
    } else if (i >= 1 && i <= 5) {
        intent.message_kind = (AutoSeqMessageKind)i;
        if (i == 3) intent.report_db = -8;
    } else if (i == 10 || i == 23 || i == 24) {
        intent.type = i == 24 ? AUTO_SEQ_TX_INTENT_CQ : AUTO_SEQ_TX_INTENT_FREETEXT;
        intent.cq_type = AUTO_SEQ_CQ_FREETEXT;
        strcpy(intent.text, i == 10 ? " 73  gl \t" : i == 23 ? "0123456789+-?" : "test  cq");
        /* Free text requires neither station nor DX/grid fields. */
        intent.callsign[0] = intent.dxcall[0] = intent.grid[0] = '\0';
    } else if (i >= 11 && i <= 14) {
        intent.flags = AUTO_SEQ_TX_INTENT_FLAG_FD;
        intent.message_kind = i % 2 ? AUTO_SEQ_MSG_TX2 : AUTO_SEQ_MSG_TX3;
        const char *exchanges[] = {"1d dx", "16B SCV", "17A EMA", "32F AB"};
        strcpy(intent.fd_exchange, exchanges[i - 11]);
    } else if (i == 15) {
        strcpy(intent.callsign, "K9XYZ/P");
    } else if (i == 16) {
        strcpy(intent.dxcall, "W1ABC/R"); intent.message_kind = AUTO_SEQ_MSG_TX3; intent.report_db = -8;
    } else if (i >= 19 && i <= 22) {
        const int8_t reports[] = {-30, 0, 49, 99};
        intent.message_kind = AUTO_SEQ_MSG_TX2; intent.report_db = reports[i - 19];
    } else if (i >= 25 && i <= 31) {
        strcpy(intent.dxcall, i == 31 ? "AG6AQ" : "W1AW/9");
        strcpy(intent.callsign, i >= 30 ? "W1AW/9" : "AG6AQ");
        strcpy(intent.grid, "CM97");
        if (i <= 29) intent.message_kind = (AutoSeqMessageKind)(i - 24);
        if (i == 27) intent.report_db = -8;
        if (i == 30) { intent.type = AUTO_SEQ_TX_INTENT_CQ; intent.cq_type = AUTO_SEQ_CQ; }
    } else CHECK(false);
    return intent;
}

static void check_channel(const Ft8TxPlan *plan)
{
    static const uint8_t costas[] = {3, 1, 4, 0, 6, 5, 2};
    static const uint8_t inverse_gray[] = {0, 1, 3, 2, 6, 4, 5, 7};
    uint8_t bits[174], decoded[174], a91[12] = {0};
    float likelihood[174];
    unsigned bit = 0;
    CHECK(sizeof(plan->tones) == 79 && !(plan->payload[9] & 7u));
    for (unsigned symbol = 0; symbol < 79; ++symbol) {
        CHECK(plan->tones[symbol] <= 7);
        if (symbol % 36 < 7) CHECK(plan->tones[symbol] == costas[symbol % 36]);
        else {
            unsigned value = inverse_gray[plan->tones[symbol]];
            for (unsigned b = 0; b < 3; ++b, ++bit) {
                bits[bit] = (uint8_t)((value >> (2 - b)) & 1u);
                likelihood[bit] = bits[bit] ? 10.0f : -10.0f;
                if (bit < 91) a91[bit / 8] |= (uint8_t)(bits[bit] << (7 - bit % 8));
            }
        }
    }
    CHECK(bit == 174);
    int errors = -1;
    ft8_ldpc_decode(likelihood, 25, decoded, &errors);
    CHECK(errors == 0 && memcmp(bits, decoded, sizeof(bits)) == 0);
    CHECK(memcmp(a91, plan->payload, 9) == 0 && (a91[9] & 0xf8u) == plan->payload[9]);
    uint16_t crc = ft8_crc_extract(a91);
    a91[9] &= 0xf8u; a91[10] = 0;
    CHECK(crc == ft8_crc_compute(a91, 82));
}

static void check_vectors(void)
{
    for (unsigned i = 0; i < 32; ++i) {
        AutoSeqTxIntent intent = vector_intent(i), original = intent;
        Ft8TxPlan plan, again;
        memset(&plan, 0xa5, sizeof(plan));
        CHECK(ft8_tx_encode(&intent, &plan) == FT8_TX_ENCODE_OK && plan.valid);
        CHECK(memcmp(&intent, &original, sizeof(intent)) == 0);
        CHECK(strcmp(plan.canonical_text, vectors[i].text) == 0);
        CHECK(memcmp(plan.payload, vectors[i].payload, sizeof(plan.payload)) == 0);
        CHECK(strlen(vectors[i].tones) == FT8_TX_TONE_COUNT);
        for (unsigned t = 0; t < FT8_TX_TONE_COUNT; ++t)
            CHECK(plan.tones[t] == (uint8_t)(vectors[i].tones[t] - '0'));
        CHECK(plan.base_hz == 1500 && plan.tx_parity == 1);
        check_channel(&plan);
        Ft8DecodedPayload payload = {0};
        memcpy(payload.payload, plan.payload, sizeof(payload.payload));
        Ft8ProtocolMessage message;
        CHECK(ft8_protocol_decode(&payload, NULL, &message) == FT8_PROTOCOL_CODEC_OK);
        if (i >= 25 && i != 30) {
            CHECK(message.type == FT8_PROTOCOL_STANDARD && message.has_unresolved_hash);
            CHECK(strstr(message.canonical_text, "<...>"));
            Ft8HashStore store; ft8_hash_store_init(&store); uint32_t hash;
            CHECK(ft8_protocol_callsign_hash22("W1AW/9", &hash) == FT8_PROTOCOL_CODEC_OK);
            CHECK(ft8_hash_store_save(&store, "W1AW/9", hash) == FT8_HASH_STORE_OK);
            CHECK(ft8_protocol_decode(&payload, &store, &message) == FT8_PROTOCOL_CODEC_OK);
            CHECK(!message.has_unresolved_hash && strstr(message.canonical_text, "<W1AW/9>"));
        } else CHECK(!message.has_unresolved_hash && strcmp(message.canonical_text, vectors[i].text) == 0);
        if (i == 30) CHECK(message.type == FT8_PROTOCOL_NONSTD_CALL && ft8_protocol_get_i3(plan.payload) == 4);
        if (i >= 11 && i <= 14) CHECK(message.type == FT8_PROTOCOL_ARRL_FD);
        if (i == 10 || i == 23 || i == 24) CHECK(message.type == FT8_PROTOCOL_FREE_TEXT);
        intent.offset_hz = -123; intent.tx_parity = 0;
        CHECK(ft8_tx_encode(&intent, &again) == FT8_TX_ENCODE_OK);
        CHECK(again.base_hz == -123 && again.tx_parity == 0);
        CHECK(memcmp(plan.payload, again.payload, sizeof(plan.payload)) == 0);
        CHECK(memcmp(plan.tones, again.tones, sizeof(plan.tones)) == 0);
        memset(&intent, 0xff, sizeof(intent));
        CHECK(plan.base_hz == 1500 && plan.tx_parity == 1);
        CHECK(strcmp(plan.canonical_text, vectors[i].text) == 0);
    }
}

static void invalid(const AutoSeqTxIntent *intent, Ft8TxEncodeStatus expected)
{
    struct { uint32_t before; Ft8TxPlan plan; uint32_t after; } guarded = {.before = 42, .after = 43};
    memset(&guarded.plan, 0xa5, sizeof(guarded.plan));
    Ft8TxEncodeStatus status = ft8_tx_encode(intent, &guarded.plan);
    if (status != expected) fprintf(stderr, "invalid call=%s status=%d expected=%d\n", intent ? intent->callsign : "NULL", status, expected);
    CHECK(status == expected);
    CHECK(guarded.before == 42 && guarded.after == 43);
    const unsigned char *bytes = (const unsigned char *)&guarded.plan;
    for (size_t i = 0; i < sizeof(guarded.plan); ++i) CHECK(bytes[i] == 0);
}

static void check_failures(void)
{
    invalid(NULL, FT8_TX_ENCODE_INVALID);
    AutoSeqTxIntent intent = qso(AUTO_SEQ_MSG_TX1);
    CHECK(ft8_tx_encode(&intent, NULL) == FT8_TX_ENCODE_INVALID);
    intent.type = AUTO_SEQ_TX_INTENT_NONE; invalid(&intent, FT8_TX_ENCODE_INVALID);
    intent = qso(AUTO_SEQ_MSG_NONE); invalid(&intent, FT8_TX_ENCODE_INVALID);
    intent = qso(AUTO_SEQ_MSG_TX6); invalid(&intent, FT8_TX_ENCODE_INVALID);
    intent = qso(AUTO_SEQ_MSG_TX1); intent.tx_parity = 2; invalid(&intent, FT8_TX_ENCODE_INVALID);
    intent = qso(AUTO_SEQ_MSG_TX1); intent.flags = 2; invalid(&intent, FT8_TX_ENCODE_INVALID);
    intent = qso(AUTO_SEQ_MSG_TX1); intent.callsign[0] = 0; invalid(&intent, FT8_TX_ENCODE_INVALID);
    intent = qso(AUTO_SEQ_MSG_TX1); intent.dxcall[0] = 0; invalid(&intent, FT8_TX_ENCODE_INVALID);
    intent = qso(AUTO_SEQ_MSG_TX1); memset(intent.callsign, 'A', sizeof(intent.callsign)); invalid(&intent, FT8_TX_ENCODE_INVALID);
    intent = qso(AUTO_SEQ_MSG_TX1); memset(intent.dxcall, 'A', sizeof(intent.dxcall)); invalid(&intent, FT8_TX_ENCODE_INVALID);
    const char *bad_calls[] = {"AB", "A", "ABCDEFGHIJKL", "<K9XYZ>", "CQ", "CQ FD", "W1!AW", "W1 AW"};
    for (unsigned i = 0; i < sizeof(bad_calls) / sizeof(bad_calls[0]); ++i) {
        intent = qso(AUTO_SEQ_MSG_TX2); strcpy(intent.callsign, bad_calls[i]);
        invalid(&intent, FT8_TX_ENCODE_UNSUPPORTED);
    }
    intent = qso(AUTO_SEQ_MSG_TX1); strcpy(intent.dxcall, "CQ"); invalid(&intent, FT8_TX_ENCODE_INVALID);
    intent = qso(AUTO_SEQ_MSG_TX1); strcpy(intent.dxcall, "W1ABC/P"); strcpy(intent.callsign, "K9XYZ/R");
    invalid(&intent, FT8_TX_ENCODE_UNSUPPORTED);
    const char *bad_grids[] = {"", "FN4", "SN42", "FN4X", "FN42ZB", "FN42A"};
    for (unsigned i = 0; i < sizeof(bad_grids) / sizeof(bad_grids[0]); ++i) {
        intent = qso(AUTO_SEQ_MSG_TX1); strcpy(intent.grid, bad_grids[i]); invalid(&intent, FT8_TX_ENCODE_INVALID);
    }
    intent = qso(AUTO_SEQ_MSG_TX1); memset(intent.grid, 'A', sizeof(intent.grid)); invalid(&intent, FT8_TX_ENCODE_INVALID);
    const int8_t bad_reports[] = {-128, -99, -35, -34, -33, -32, -31, 100, 127};
    for (unsigned i = 0; i < sizeof(bad_reports) / sizeof(bad_reports[0]); ++i) {
        intent = qso(AUTO_SEQ_MSG_TX2); intent.report_db = bad_reports[i]; invalid(&intent, FT8_TX_ENCODE_INVALID);
        intent.message_kind = AUTO_SEQ_MSG_TX3; invalid(&intent, FT8_TX_ENCODE_INVALID);
    }
    const char *bad_exchange[] = {"", "0A DX", "33A DX", "1G DX", "1D BAD", "1DDX", "1D DX EXTRA", "1D", "R 1D DX"};
    for (unsigned i = 0; i < sizeof(bad_exchange) / sizeof(bad_exchange[0]); ++i) {
        intent = vector_intent(11); strcpy(intent.fd_exchange, bad_exchange[i]); invalid(&intent, FT8_TX_ENCODE_INVALID);
    }
    intent = vector_intent(11); memset(intent.fd_exchange, 'A', sizeof(intent.fd_exchange)); invalid(&intent, FT8_TX_ENCODE_INVALID);
    intent = vector_intent(11); strcpy(intent.callsign, "K9XYZ/P"); invalid(&intent, FT8_TX_ENCODE_UNSUPPORTED);
    intent = vector_intent(0); intent.cq_type = 255; invalid(&intent, FT8_TX_ENCODE_INVALID);
    intent = vector_intent(10); intent.text[0] = 0; invalid(&intent, FT8_TX_ENCODE_INVALID);
    intent = vector_intent(10); strcpy(intent.text, "   \t \n"); invalid(&intent, FT8_TX_ENCODE_INVALID);
    intent = vector_intent(10); strcpy(intent.text, "HELLO!"); invalid(&intent, FT8_TX_ENCODE_INVALID);
    intent = vector_intent(10); memset(intent.text, 'A', sizeof(intent.text)); invalid(&intent, FT8_TX_ENCODE_INVALID);
    for (unsigned cq = AUTO_SEQ_CQ_SOTA; cq <= AUTO_SEQ_CQ_FD; ++cq) {
        intent = vector_intent(30); intent.cq_type = (AutoSeqCqType)cq;
        invalid(&intent, FT8_TX_ENCODE_UNSUPPORTED);
    }
    uint8_t payload[10]; Ft8ProtocolMessage message = {.type = FT8_PROTOCOL_NONSTD_CALL};
    memset(payload, 0xa5, sizeof(payload));
    CHECK(ft8_protocol_encode(&message, payload) == FT8_PROTOCOL_CODEC_UNSUPPORTED);
    for (unsigned i = 0; i < sizeof(payload); ++i) CHECK(!payload[i]);
}

static void check_nonstandard_typed(void)
{
    Ft8ProtocolMessage typed = {.type = FT8_PROTOCOL_NONSTD_CALL}, decoded;
    strcpy(typed.data.nonstandard.call_to, "AG6AQ");
    strcpy(typed.data.nonstandard.call_de, "W1AW/9");
    Ft8DecodedPayload payload = {0};
    for (unsigned t = 0; t <= FT8_PROTOCOL_TERMINAL_73; ++t) {
        typed.data.nonstandard.terminal = (Ft8ProtocolTerminal)t;
        CHECK(ft8_protocol_encode(&typed, payload.payload) == FT8_PROTOCOL_CODEC_OK);
        CHECK(ft8_protocol_decode(&payload, NULL, &decoded) == FT8_PROTOCOL_CODEC_OK);
        CHECK(memcmp(payload.payload, vectors[32 + t].payload, sizeof(payload.payload)) == 0);
        uint8_t tones[79]; ft8_tx_channel_encode(payload.payload, tones);
        for (unsigned i = 0; i < 79; ++i) CHECK(tones[i] == vectors[32 + t].tones[i] - '0');
        CHECK(decoded.type == typed.type && decoded.has_unresolved_hash);
        CHECK(decoded.data.nonstandard.terminal == t);
        CHECK(strcmp(decoded.data.nonstandard.call_de, "W1AW/9") == 0);
    }
    typed.data.nonstandard.terminal = (Ft8ProtocolTerminal)4;
    CHECK(ft8_protocol_encode(&typed, payload.payload) == FT8_PROTOCOL_CODEC_MALFORMED);
    for (unsigned i = 0; i < sizeof(payload.payload); ++i) CHECK(!payload.payload[i]);
    typed.data.nonstandard.is_cq = true;
    strcpy(typed.data.nonstandard.call_to, "CQ");
    typed.data.nonstandard.terminal = FT8_PROTOCOL_TERMINAL_73;
    CHECK(ft8_protocol_encode(&typed, payload.payload) == FT8_PROTOCOL_CODEC_MALFORMED);
    AutoSeqTxIntent intent = vector_intent(11); Ft8TxPlan plan;
    strcpy(intent.dxcall, "W1AW/9");
    CHECK(ft8_tx_encode(&intent, &plan) == FT8_TX_ENCODE_OK);
    CHECK(ft8_protocol_get_type(plan.payload) == FT8_PROTOCOL_ARRL_FD);
    CHECK(strcmp(plan.canonical_text, "W1AW/9 K9XYZ 1D DX") == 0);
}

int main(void)
{
    CHECK(FT8_TX_SYMBOL_PERIOD_MS == 160u && FT8_TX_TONE_SPACING_HZ == 6.25f);
    const float hz[] = {1500, 1506.25f, 1512.5f, 1518.75f, 1525, 1531.25f, 1537.5f, 1543.75f};
    for (unsigned i = 0; i < 8; ++i) CHECK(ft8_tx_tone_hz(1500, (uint8_t)i) == hz[i]);
    check_vectors();
    check_nonstandard_typed();
    check_failures();
    AutoSeqTxIntent intent = qso(AUTO_SEQ_MSG_TX1); Ft8TxPlan plan;
    strcpy(intent.callsign, "k9xyz"); strcpy(intent.grid, "fn42ab");
    CHECK(ft8_tx_encode(&intent, &plan) == FT8_TX_ENCODE_OK);
    CHECK(strcmp(plan.canonical_text, "W1ABC K9XYZ FN42") == 0);
    /* Field Day changes only TX2/TX3, including no exchange requirement for TX4/TX5. */
    for (unsigned kind = AUTO_SEQ_MSG_TX1; kind <= AUTO_SEQ_MSG_TX5; ++kind) {
        if (kind == AUTO_SEQ_MSG_TX2 || kind == AUTO_SEQ_MSG_TX3) continue;
        intent = qso(kind); intent.flags = AUTO_SEQ_TX_INTENT_FLAG_FD;
        CHECK(ft8_tx_encode(&intent, &plan) == FT8_TX_ENCODE_OK);
        CHECK(strcmp(plan.canonical_text, vectors[kind].text) == 0);
    }
    puts("FT8 TX: 36 fixed V2 payload/tone vectors, semantic projection, LDPC/CRC roundtrip, invalid-plan and immutable snapshot checks PASS");
    return 0;
}

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "config_service.h"
#include "auto_seq_tx_intent.h"
#include "tx_encoder.h"
#include "ft8_message_codec.h"

static void parsing(void)
{
    ConfigService config, again;
    char saved[2048];
    const char *expected[] = {"", "POTA", "SOTA", "DX", "250"};
    assert(config_service_parse(&config, "cq_type=4\ncqtypes=pota  Sota DX 250 POTA dx ABCDE 25 2500 DX1 P0TA POTA/ FREETEXT\n"));
    assert(config.cq_type == 4 && config.cq_modifier_count == 4);
    for (unsigned i = 0; i < 5; ++i)
        assert(strcmp(config_service_cq_modifier(&config, i), expected[i]) == 0);
    assert(config_service_serialize(&config, saved, sizeof(saved)));
    assert(strstr(saved, "cq_type=4\ncqtypes=POTA SOTA DX 250\n"));
    assert(!strstr(saved, "ABCDE") && !strstr(saved, "FREETEXT"));
    assert(config_service_parse(&again, saved));
    assert(memcmp(&config, &again, sizeof(config)) == 0);
    assert(config_service_parse(&config, "cqtypes=POTA SOTA DX 250\ncq_type=2\n"));
    assert(config.cq_type == 2 && strcmp(config_service_cq_modifier(&config, 2), "SOTA") == 0);
    assert(config_service_parse(&config, "cq_type=2\n"));
    assert(config.cq_type == 2 && config.cq_modifier_count == 4);
    assert(strcmp(config_service_cq_modifier(&config, 2), "POTA") == 0);
    const char *plain[] = {"", "cqtypes=\n", "cqtypes=   \n", "cqtypes=ABCDE 25 2500 DX1 P0TA POTA/ FREETEXT\n"};
    for (unsigned i = 0; i < sizeof(plain) / sizeof(plain[0]); ++i) {
        assert(config_service_parse(&config, plain[i]));
        assert(config.cq_type == 0 && strcmp(config_service_cq_modifier(&config, 0), "") == 0);
        if (i) assert(config.cq_modifier_count == 0);
    }
    const char *invalid[] = {"-1", "5", "17", "256", "999999999999999999999", "bad", "2x"};
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        char text[128]; snprintf(text, sizeof(text), "cq_type=%s\n", invalid[i]);
        assert(config_service_parse(&config, text) && config.cq_type == 0);
    }
    assert(config_service_parse(&config, "cqtypes=A B C D E F G H I J K L M N O P Q R\ncq_type=16\n"));
    assert(config.cq_modifier_count == 16 && config.cq_type == 16);
    assert(strcmp(config_service_cq_modifier(&config, 16), "P") == 0);
    assert(!config_service_set_cq_type(&config, 17));
    assert(config_service_serialize(&config, saved, sizeof(saved)));
    assert(strstr(saved, "cqtypes=A B C D E F G H I J K L M N O P\n"));
    assert(config_service_parse(&config, "cqtypes=000 999 Z ABCD FD QRP\n"));
    assert(config.cq_modifier_count == 6);
}

static void round_trips(void)
{
    ConfigService config;
    assert(config_service_parse(&config, "cqtypes=POTA SOTA DX 250 FD QRP\n"));
    const char *tokens[] = {"CQ", "CQ POTA", "CQ SOTA", "CQ DX", "CQ 250", "CQ FD", "CQ QRP"};
    const AutoSeqCqType types[] = {AUTO_SEQ_CQ, AUTO_SEQ_CQ_POTA, AUTO_SEQ_CQ_SOTA,
        AUTO_SEQ_CQ_MODIFIER, AUTO_SEQ_CQ_MODIFIER, AUTO_SEQ_CQ_FD, AUTO_SEQ_CQ_QRP};
    for (unsigned i = 0; i < 7; ++i) {
        AutoSeq seq; AutoSeqTxIntent intent; Ft8TxPlan plan;
        assert(auto_seq_init(&seq, NULL));
        assert(auto_seq_set_station(&seq, "AG6AQ", "CM97"));
        assert(auto_seq_set_fd_exchange(&seq, "1B SCV"));
        assert(auto_seq_set_cq_modifier(&seq, config_service_cq_modifier(&config, i)));
        assert(auto_seq_start_cq(&seq, 1) == AUTO_SEQ_OK);
        assert(auto_seq_prepare_tx_intent(&seq, &intent));
        assert(intent.cq_type == types[i]);
        assert(!!(intent.flags & AUTO_SEQ_TX_INTENT_FLAG_FD) == (i == 5));
        if (i == 5) assert(strcmp(intent.fd_exchange, "1B SCV") == 0);
        assert(ft8_tx_encode(&intent, &plan) == FT8_TX_ENCODE_OK);
        char expected[64]; snprintf(expected, sizeof(expected), "%s AG6AQ CM97", tokens[i]);
        assert(strcmp(plan.canonical_text, expected) == 0);
        Ft8DecodedPayload payload = {0}; Ft8ProtocolMessage decoded;
        memcpy(payload.payload, plan.payload, sizeof(payload.payload));
        assert(ft8_protocol_decode(&payload, NULL, &decoded) == FT8_PROTOCOL_CODEC_OK);
        assert(decoded.type == FT8_PROTOCOL_STANDARD && !decoded.has_unresolved_hash);
        assert(strcmp(decoded.canonical_text, expected) == 0);
        strcpy(intent.callsign, "W1AW/9");
        assert(ft8_tx_encode(&intent, &plan) == (i ? FT8_TX_ENCODE_UNSUPPORTED : FT8_TX_ENCODE_OK));
        /* Free-text CQ remains a separate intent, never a modifier token. */
        assert(auto_seq_set_cq(&seq, AUTO_SEQ_CQ_FREETEXT, "TEST CQ"));
        assert(auto_seq_prepare_tx_intent(&seq, &intent));
        assert(ft8_tx_encode(&intent, &plan) == FT8_TX_ENCODE_OK);
        assert(strcmp(plan.canonical_text, "TEST CQ") == 0);
    }
    AutoSeq seq; assert(auto_seq_init(&seq, NULL));
    assert(!auto_seq_set_cq_modifier(&seq, "FREETEXT"));
    assert(!auto_seq_set_cq_modifier(&seq, "DX1"));
}

int main(void)
{
    parsing(); round_trips();
    puts("CQ list parsing, semantics, persistence and codec round-trips: PASS");
    return 0;
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ft8_message_codec.h"

#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        return 1; \
    } \
} while (0)

#define NTOKENS UINT32_C(2063592)

static void decoded_init(Ft8DecodedPayload *decoded, const uint8_t payload[FT8_PAYLOAD_BYTES])
{
    memset(decoded, 0, sizeof(*decoded));
    memcpy(decoded->payload, payload, FT8_PAYLOAD_BYTES);
}

static void set_bits_be(uint8_t payload[FT8_PAYLOAD_BYTES],
                        unsigned start_bit,
                        unsigned nbits,
                        uint32_t value)
{
    unsigned i;
    for (i = 0; i < nbits; ++i) {
        unsigned bitpos = start_bit + i;
        unsigned byte = bitpos >> 3;
        unsigned bit_in_byte = 7u - (bitpos & 7u);
        uint8_t mask = (uint8_t)(1u << bit_in_byte);
        uint32_t src = (value >> (nbits - 1u - i)) & 1u;
        if (src != 0u)
            payload[byte] |= mask;
        else
            payload[byte] &= (uint8_t)~mask;
    }
}

static int save_named_hash(Ft8HashStore *store, const char *callsign, uint32_t *out_hash22)
{
    uint32_t hash22 = 0;
    CHECK(ft8_protocol_callsign_hash22(callsign, &hash22) == FT8_PROTOCOL_CODEC_OK);
    CHECK(ft8_hash_store_save(store, callsign, hash22) == FT8_HASH_STORE_OK);
    if (out_hash22 != NULL)
        *out_hash22 = hash22;
    return 0;
}

static int test_standard_cq(void)
{
    static const uint8_t payload[FT8_PAYLOAD_BYTES] = {
        0x00,0x00,0x00,0x20,0x60,0x16,0x50,0x0A,0x19,0x88
    };
    Ft8DecodedPayload decoded;
    Ft8ProtocolMessage message;
    Ft8HashStore store;
    uint32_t w1xyz_hash;
    char found[FT8_HASH_STORE_CALLSIGN_CAP];

    ft8_hash_store_init(&store);
    decoded_init(&decoded, payload);
    decoded.candidate.score = 17;
    decoded.candidate.time_offset = 3;
    decoded.candidate.freq_offset = 211;
    decoded.ldpc_errors = 0;
    decoded.crc_extracted = 0x1234u;
    decoded.crc_calculated = 0x1234u;

    CHECK(ft8_protocol_get_i3(payload) == 1u);
    CHECK(ft8_protocol_get_n3(payload) == 6u);
    CHECK(ft8_protocol_get_type(payload) == FT8_PROTOCOL_STANDARD);
    CHECK(ft8_protocol_decode(&decoded, &store, &message) == FT8_PROTOCOL_CODEC_OK);
    CHECK(message.parse_status == FT8_PROTOCOL_PARSE_OK);
    CHECK(message.type == FT8_PROTOCOL_STANDARD);
    CHECK(strcmp(message.canonical_text, "CQ W1XYZ FN42") == 0);
    CHECK(strcmp(message.data.standard.call_to, "CQ") == 0);
    CHECK(message.data.standard.call_to_kind == FT8_PROTOCOL_FIELD_TOKEN);
    CHECK(strcmp(message.data.standard.call_de, "W1XYZ") == 0);
    CHECK(strcmp(message.data.standard.extra, "FN42") == 0);
    CHECK(message.data.standard.extra_kind == FT8_PROTOCOL_FIELD_GRID);
    CHECK(!message.has_unresolved_hash);
    CHECK(message.candidate.score == 17);
    CHECK(message.candidate.time_offset == 3);
    CHECK(message.candidate.freq_offset == 211);
    CHECK(message.crc_extracted == 0x1234u);
    CHECK(message.crc_calculated == 0x1234u);

    CHECK(ft8_protocol_callsign_hash22("W1XYZ", &w1xyz_hash) == FT8_PROTOCOL_CODEC_OK);
    CHECK(ft8_hash_store_lookup(&store, FT8_HASH_22_BITS, w1xyz_hash,
                                found, sizeof(found)) == FT8_HASH_STORE_OK);
    CHECK(strcmp(found, "W1XYZ") == 0);
    return 0;
}

static int test_arrl_fd(void)
{
    static const uint8_t payload[FT8_PAYLOAD_BYTES] = {
        0x0C,0x16,0x90,0xC5,0x36,0xB4,0x26,0x81,0x76,0xC0
    };
    Ft8DecodedPayload decoded;
    Ft8ProtocolMessage message;
    Ft8HashStore store;

    ft8_hash_store_init(&store);
    decoded_init(&decoded, payload);
    CHECK(ft8_protocol_decode(&decoded, &store, &message) == FT8_PROTOCOL_CODEC_OK);
    CHECK(message.type == FT8_PROTOCOL_ARRL_FD);
    CHECK(strcmp(message.canonical_text, "W6ABC AG6AQ R 1B SCV") == 0);
    CHECK(strcmp(message.data.arrl_fd.call_to, "W6ABC") == 0);
    CHECK(strcmp(message.data.arrl_fd.call_de, "AG6AQ") == 0);
    CHECK(message.data.arrl_fd.has_r);
    CHECK(message.data.arrl_fd.transmitter_count == 1u);
    CHECK(message.data.arrl_fd.class_letter == 'B');
    CHECK(strcmp(message.data.arrl_fd.section, "SCV") == 0);
    return 0;
}

static int test_dxpedition_hash_hit_and_miss(void)
{
    static const uint8_t payload[FT8_PAYLOAD_BYTES] = {
        0x32,0x6C,0x13,0x7B,0xC6,0xA1,0x85,0x27,0x70,0x40
    };
    Ft8DecodedPayload decoded;
    Ft8ProtocolMessage message;
    Ft8HashStore store;
    uint32_t fox_hash;

    decoded_init(&decoded, payload);
    ft8_hash_store_init(&store);
    CHECK(save_named_hash(&store, "KH1/KH7Z", &fox_hash) == 0);
    CHECK((fox_hash >> 12) == 0x0C9u);

    CHECK(ft8_protocol_decode(&decoded, &store, &message) == FT8_PROTOCOL_CODEC_OK);
    CHECK(message.type == FT8_PROTOCOL_DXPEDITION);
    CHECK(strcmp(message.canonical_text,
                 "K1ABC RR73; W9XYZ <KH1/KH7Z> -08") == 0);
    CHECK(strcmp(message.data.dxpedition.rr73_call, "K1ABC") == 0);
    CHECK(strcmp(message.data.dxpedition.report_call, "W9XYZ") == 0);
    CHECK(strcmp(message.data.dxpedition.fox_call, "<KH1/KH7Z>") == 0);
    CHECK(message.data.dxpedition.report_db == -8);
    CHECK(!message.has_unresolved_hash);

    ft8_hash_store_clear(&store);
    CHECK(ft8_protocol_decode(&decoded, &store, &message) == FT8_PROTOCOL_CODEC_OK);
    CHECK(strcmp(message.canonical_text,
                 "K1ABC RR73; W9XYZ <...> -08") == 0);
    CHECK(message.has_unresolved_hash);
    return 0;
}

static int test_nonstandard_cq(void)
{
    static const uint8_t payload[FT8_PAYLOAD_BYTES] = {
        0x00,0x00,0x3E,0x4A,0x34,0xA8,0x6E,0xEB,0x84,0x60
    };
    Ft8DecodedPayload decoded;
    Ft8ProtocolMessage message;
    Ft8HashStore store;

    decoded_init(&decoded, payload);
    ft8_hash_store_init(&store);
    CHECK(ft8_protocol_decode(&decoded, &store, &message) == FT8_PROTOCOL_CODEC_OK);
    CHECK(message.type == FT8_PROTOCOL_NONSTD_CALL);
    CHECK(strcmp(message.canonical_text, "CQ PJ4/KA1ABC") == 0);
    CHECK(message.data.nonstandard.is_cq);
    CHECK(strcmp(message.data.nonstandard.call_to, "CQ") == 0);
    CHECK(strcmp(message.data.nonstandard.call_de, "PJ4/KA1ABC") == 0);
    CHECK(message.data.nonstandard.terminal == FT8_PROTOCOL_TERMINAL_NONE);
    return 0;
}

static int test_free_text(void)
{
    static const uint8_t payload[FT8_PAYLOAD_BYTES] = {
        0x2C,0x91,0x49,0x5D,0x9F,0x3A,0x73,0x11,0x94,0x00
    };
    Ft8DecodedPayload decoded;
    Ft8ProtocolMessage message;

    decoded_init(&decoded, payload);
    CHECK(ft8_protocol_decode(&decoded, NULL, &message) == FT8_PROTOCOL_CODEC_OK);
    CHECK(message.type == FT8_PROTOCOL_FREE_TEXT);
    CHECK(strcmp(message.canonical_text, "CQ POTA W1XYZ") == 0);
    CHECK(strcmp(message.data.free_text.text, "CQ POTA W1XYZ") == 0);
    return 0;
}

static int test_telemetry(void)
{
    static const uint8_t telemetry[FT8_PROTOCOL_TELEMETRY_BYTES] = {
        0x01,0x23,0x45,0x67,0x89,0xAB,0xCD,0xEF,0x01
    };
    Ft8DecodedPayload decoded;
    Ft8ProtocolMessage message;
    uint8_t carry = 0;
    int i;

    memset(&decoded, 0, sizeof(decoded));
    for (i = 8; i >= 0; --i) {
        decoded.payload[i] = (uint8_t)((telemetry[i] << 1) | (carry >> 7));
        carry = (uint8_t)(telemetry[i] & 0x80u);
    }
    decoded.payload[8] = (uint8_t)((decoded.payload[8] & 0xFEu) | 0x01u);
    decoded.payload[9] = 0x40u;

    CHECK(ft8_protocol_get_type(decoded.payload) == FT8_PROTOCOL_TELEMETRY);
    CHECK(ft8_protocol_decode(&decoded, NULL, &message) == FT8_PROTOCOL_CODEC_OK);
    CHECK(strcmp(message.canonical_text, "0123456789ABCDEF01") == 0);
    CHECK(strcmp(message.data.telemetry.hex, "0123456789ABCDEF01") == 0);
    CHECK(memcmp(message.data.telemetry.bytes, telemetry, sizeof(telemetry)) == 0);
    return 0;
}

static int test_22bit_hash_standard(void)
{
    static const uint8_t cq_payload[FT8_PAYLOAD_BYTES] = {
        0x00,0x00,0x00,0x20,0x60,0x16,0x50,0x0A,0x19,0x88
    };
    Ft8DecodedPayload decoded;
    Ft8ProtocolMessage message;
    Ft8HashStore store;
    uint32_t hash22;
    uint32_t n29;

    decoded_init(&decoded, cq_payload);
    ft8_hash_store_init(&store);
    CHECK(save_named_hash(&store, "AG6AQ", &hash22) == 0);
    n29 = (NTOKENS + hash22) << 1;
    set_bits_be(decoded.payload, 0, 29, n29);

    CHECK(ft8_protocol_decode(&decoded, &store, &message) == FT8_PROTOCOL_CODEC_OK);
    CHECK(strcmp(message.canonical_text, "<AG6AQ> W1XYZ FN42") == 0);
    CHECK(!message.has_unresolved_hash);

    ft8_hash_store_clear(&store);
    CHECK(ft8_protocol_decode(&decoded, &store, &message) == FT8_PROTOCOL_CODEC_OK);
    CHECK(strcmp(message.canonical_text, "<...> W1XYZ FN42") == 0);
    CHECK(message.has_unresolved_hash);
    return 0;
}

static int test_12bit_hash_nonstandard(void)
{
    static const uint8_t cq_payload[FT8_PAYLOAD_BYTES] = {
        0x00,0x00,0x3E,0x4A,0x34,0xA8,0x6E,0xEB,0x84,0x60
    };
    Ft8DecodedPayload decoded;
    Ft8ProtocolMessage message;
    Ft8HashStore store;
    uint32_t hash22;

    decoded_init(&decoded, cq_payload);
    ft8_hash_store_init(&store);
    CHECK(save_named_hash(&store, "AG6AQ", &hash22) == 0);

    set_bits_be(decoded.payload, 0, 12, hash22 >> 10);
    set_bits_be(decoded.payload, 70, 1, 0u); /* iflip */
    set_bits_be(decoded.payload, 71, 2, 2u); /* RR73 */
    set_bits_be(decoded.payload, 73, 1, 0u); /* directed, not CQ */
    set_bits_be(decoded.payload, 74, 3, 4u);

    CHECK(ft8_protocol_decode(&decoded, &store, &message) == FT8_PROTOCOL_CODEC_OK);
    CHECK(strcmp(message.canonical_text, "<AG6AQ> PJ4/KA1ABC RR73") == 0);
    CHECK(!message.data.nonstandard.is_cq);
    CHECK(message.data.nonstandard.terminal == FT8_PROTOCOL_TERMINAL_RR73);
    CHECK(!message.has_unresolved_hash);
    return 0;
}

static int test_unsupported_and_malformed(void)
{
    Ft8DecodedPayload decoded;
    Ft8ProtocolMessage message;
    static const uint8_t fd_payload[FT8_PAYLOAD_BYTES] = {
        0x0C,0x16,0x90,0xC5,0x36,0xB4,0x26,0x81,0x76,0xC0
    };

    memset(&decoded, 0, sizeof(decoded));
    set_bits_be(decoded.payload, 74, 3, 3u);
    CHECK(ft8_protocol_get_type(decoded.payload) == FT8_PROTOCOL_ARRL_RTTY);
    CHECK(ft8_protocol_decode(&decoded, NULL, &message) == FT8_PROTOCOL_CODEC_UNSUPPORTED);
    CHECK(message.parse_status == FT8_PROTOCOL_PARSE_UNSUPPORTED);
    CHECK(message.canonical_text[0] == '\0');

    memset(&decoded, 0, sizeof(decoded));
    set_bits_be(decoded.payload, 71, 3, 6u);
    set_bits_be(decoded.payload, 74, 3, 0u);
    CHECK(ft8_protocol_get_type(decoded.payload) == FT8_PROTOCOL_UNKNOWN);
    CHECK(ft8_protocol_decode(&decoded, NULL, &message) == FT8_PROTOCOL_CODEC_UNSUPPORTED);

    decoded_init(&decoded, fd_payload);
    set_bits_be(decoded.payload, 61, 3, 7u); /* invalid class */
    CHECK(ft8_protocol_decode(&decoded, NULL, &message) == FT8_PROTOCOL_CODEC_MALFORMED);
    CHECK(message.parse_status == FT8_PROTOCOL_PARSE_MALFORMED);
    return 0;
}

static int test_protocol_slot(void)
{
    static const uint8_t payload_a[FT8_PAYLOAD_BYTES] = {
        0x00,0x00,0x00,0x20,0x60,0x16,0x50,0x0A,0x19,0x88
    };
    static const uint8_t payload_b[FT8_PAYLOAD_BYTES] = {
        0x2C,0x91,0x49,0x5D,0x9F,0x3A,0x73,0x11,0x94,0x00
    };
    Ft8DecodedPayload decoded_a;
    Ft8DecodedPayload decoded_b;
    Ft8ProtocolMessage storage[2];
    Ft8ProtocolSlot slot;
    Ft8ProtocolCodecStatus codec_status;

    decoded_init(&decoded_a, payload_a);
    decoded_a.candidate.score = 19;
    decoded_init(&decoded_b, payload_b);

    ft8_protocol_slot_init(&slot, 1234, storage, 2);
    CHECK(slot.slot_id == 1234);
    CHECK(slot.status == FT8_PROTOCOL_SLOT_EMPTY);
    CHECK(slot.message_count == 0u);

    CHECK(ft8_protocol_slot_decode_add(&slot, &decoded_a, NULL, &codec_status) ==
          FT8_PROTOCOL_SLOT_ADDED);
    CHECK(codec_status == FT8_PROTOCOL_CODEC_OK);
    CHECK(slot.status == FT8_PROTOCOL_SLOT_OK);
    CHECK(slot.message_count == 1u);
    CHECK(storage[0].candidate.score == 19);

    CHECK(ft8_protocol_slot_decode_add(&slot, &decoded_a, NULL, &codec_status) ==
          FT8_PROTOCOL_SLOT_DUPLICATE);
    CHECK(slot.message_count == 1u);

    CHECK(ft8_protocol_slot_decode_add(&slot, &decoded_b, NULL, &codec_status) ==
          FT8_PROTOCOL_SLOT_ADDED);
    CHECK(slot.message_count == 2u);
    CHECK(strcmp(storage[1].canonical_text, "CQ POTA W1XYZ") == 0);

    decoded_b.payload[0] ^= 0x80u;
    CHECK(ft8_protocol_slot_decode_add(&slot, &decoded_b, NULL, &codec_status) ==
          FT8_PROTOCOL_SLOT_ERR_FULL);
    CHECK(slot.status == FT8_PROTOCOL_SLOT_FULL);
    CHECK(slot.message_count == 2u);
    return 0;
}

int main(void)
{
    CHECK(test_standard_cq() == 0);
    CHECK(test_arrl_fd() == 0);
    CHECK(test_dxpedition_hash_hit_and_miss() == 0);
    CHECK(test_nonstandard_cq() == 0);
    CHECK(test_free_text() == 0);
    CHECK(test_telemetry() == 0);
    CHECK(test_22bit_hash_standard() == 0);
    CHECK(test_12bit_hash_nonstandard() == 0);
    CHECK(test_unsupported_and_malformed() == 0);
    CHECK(test_protocol_slot() == 0);

    printf("PASS: RX-1F typed protocol codec\n");
    return 0;
}

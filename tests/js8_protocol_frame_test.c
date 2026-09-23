#include "js8_protocol_frame.h"
#include "js8_channel.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

struct golden {
    const char *payload;
    unsigned crc;
    const char *info;
    const char *codeword;
    uint8_t tones[JS8_TONE_COUNT];
};
#include "js8_golden_vectors.h"

static void unpack(const char *text, uint8_t bits[75])
{
    assert(strlen(text) == 75);
    for (unsigned i = 0; i < 75; ++i) bits[i] = (uint8_t)(text[i] - '0');
}

int main(void)
{
    /* v3.0.3 Varicode.h FrameType/TransmissionType, independently enumerated. */
    static const Js8AppFrameClass classes[8] = {
        JS8_APP_FRAME_HEARTBEAT, JS8_APP_FRAME_COMPOUND,
        JS8_APP_FRAME_COMPOUND_DIRECTED, JS8_APP_FRAME_DIRECTED,
        JS8_APP_FRAME_DATA, JS8_APP_FRAME_DATA,
        JS8_APP_FRAME_DATA_COMPRESSED, JS8_APP_FRAME_DATA_COMPRESSED
    };
    static const char *const names[8] = {
        "heartbeat", "compound", "compound_directed", "directed",
        "data", "data", "data_compressed", "data_compressed"
    };
    static const struct { int first, last, data; const char *name; } flags[8] = {
        {0,0,0,"NONE"}, {1,0,0,"FIRST"}, {0,1,0,"LAST"}, {1,1,0,"FIRST|LAST"},
        {0,0,1,"DATA"}, {1,0,1,"FIRST|DATA"}, {0,1,1,"LAST|DATA"}, {1,1,1,"FIRST|LAST|DATA"}
    };
    assert(JS8_TX_FLAG_FIRST == 1 && JS8_TX_FLAG_LAST == 2 && JS8_TX_FLAG_DATA == 4);
    uint8_t bits[75];
    Js8ProtocolEnvelope envelope;
    for (unsigned middle = 0; middle < 2; ++middle) {
        for (unsigned prefix = 0; prefix < 8; ++prefix) {
            for (unsigned tx = 0; tx < 8; ++tx) {
                memset(bits, (int)middle, sizeof(bits));
                for (unsigned i = 0; i < 3; ++i) {
                    bits[i] = (prefix >> (2-i)) & 1;
                    bits[72+i] = (tx >> (2-i)) & 1;
                }
                uint8_t saved[75]; memcpy(saved, bits, sizeof(bits));
                assert(js8_protocol_envelope_decode(bits, &envelope) == 0);
                assert(envelope.app_class == classes[prefix]);
                assert(envelope.raw_app_prefix3 == prefix && envelope.tx_flags == tx);
                assert(envelope.first == flags[tx].first && envelope.last == flags[tx].last);
                assert(envelope.data_flag == flags[tx].data);
                assert(!strcmp(js8_app_frame_class_name(envelope.app_class), names[prefix]));
                assert(!strcmp(js8_tx_flags_name(envelope.tx_flags), flags[tx].name));
                assert(!memcmp(saved, bits, sizeof(bits)));
            }
        }
    }
    static const Js8AppFrameClass golden_classes[3] = {
        JS8_APP_FRAME_HEARTBEAT, JS8_APP_FRAME_COMPOUND_DIRECTED, JS8_APP_FRAME_COMPOUND
    };
    static const uint8_t golden_tx[3] = {0, 2, 3};
    for (unsigned repeat = 0; repeat < 2; ++repeat)
        for (unsigned i = 0; i < 3; ++i) {
            unpack(vectors[i].payload, bits);
            assert(js8_protocol_envelope_decode(bits, &envelope) == 0);
            assert(envelope.app_class == golden_classes[i] && envelope.tx_flags == golden_tx[i]);
        }
    unpack("111001011101001010000111001011100000101011000001100010000111111111111111010", bits);
    assert(js8_protocol_envelope_decode(bits, &envelope) == 0);
    assert(envelope.app_class == JS8_APP_FRAME_DATA_COMPRESSED);
    assert(envelope.raw_app_prefix3 == 7 && envelope.tx_flags == JS8_TX_FLAG_LAST);
    assert(!envelope.first && envelope.last && !envelope.data_flag);
    memset(&envelope, 0xa5, sizeof(envelope));
    assert(js8_protocol_envelope_decode(NULL, &envelope) == -1);
    assert(js8_protocol_envelope_decode(bits, NULL) == -1);
    for (unsigned i = 0; i < 75; ++i) {
        uint8_t saved = bits[i];
        for (unsigned value = 2; value < 256; ++value) {
            bits[i] = (uint8_t)value;
            assert(js8_protocol_envelope_decode(bits, &envelope) == -1);
            const unsigned char *p = (const unsigned char *)&envelope;
            for (size_t j = 0; j < sizeof(envelope); ++j) assert(p[j] == 0xa5);
        }
        bits[i] = saved;
    }
    assert(!strcmp(js8_app_frame_class_name((Js8AppFrameClass)5), "unknown"));
    assert(!strcmp(js8_app_frame_class_name((Js8AppFrameClass)-1), "unknown"));
    for (unsigned i = 8; i < 256; ++i) assert(!strcmp(js8_tx_flags_name((uint8_t)i), "INVALID"));
    puts("js8_protocol_frame_test: PASS");
    return 0;
}

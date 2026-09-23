#include "js8_channel.h"
#include "js8_crc.h"
#include "js8_ldpc.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
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

static void unpack(const char *text, uint8_t *bits, unsigned count)
{
    assert(strlen(text) == count);
    for (unsigned i = 0; i < count; ++i) {
        assert(text[i] == '0' || text[i] == '1');
        bits[i] = (uint8_t)(text[i] - '0');
    }
}

static void golden_tests(void)
{
    for (unsigned v = 0; v < sizeof(vectors) / sizeof(vectors[0]); ++v) {
        uint8_t payload[JS8_PAYLOAD_BITS], expected_info[JS8_INFO_BITS];
        uint8_t expected_cw[JS8_CODEWORD_BITS];
        struct { uint8_t before, bits[JS8_INFO_BITS], after; } info = {0};
        struct { uint8_t before, bits[JS8_CODEWORD_BITS], after; } cw = {0};
        info.before = info.after = cw.before = cw.after = 0xa5;
        unpack(vectors[v].payload, payload, JS8_PAYLOAD_BITS);
        unpack(vectors[v].info, expected_info, JS8_INFO_BITS);
        unpack(vectors[v].codeword, expected_cw, JS8_CODEWORD_BITS);
        assert(js8_crc12_append(payload, info.bits) == 0);
        assert(memcmp(info.bits, expected_info, JS8_INFO_BITS) == 0);
        unsigned crc = 0;
        for (unsigned i = JS8_PAYLOAD_BITS; i < JS8_INFO_BITS; ++i)
            crc = (crc << 1) | info.bits[i];
        assert(crc == vectors[v].crc);
        assert(js8_crc12_check(info.bits) == 1);
        assert(js8_ldpc_encode(expected_info, cw.bits) == 0);
        assert(memcmp(cw.bits, expected_cw, JS8_CODEWORD_BITS) == 0);
        assert(memcmp(cw.bits + JS8_INFO_BITS, expected_info, JS8_INFO_BITS) == 0);

        for (unsigned damaged = 0; damaged < 2; ++damaged) {
            float llr[JS8_CODEWORD_BITS];
            for (unsigned i = 0; i < JS8_CODEWORD_BITS; ++i)
                llr[i] = expected_cw[i] ? 4.0f : -4.0f;
            if (damaged) {
                const unsigned flips[] = {0, 87, 173};
                for (unsigned i = 0; i < 3; ++i)
                    llr[flips[i]] = expected_cw[flips[i]] ? -0.25f : 0.25f;
            }
            /* Repeated calls must not retain any decoder state. */
            for (unsigned repeat = 0; repeat < 2; ++repeat) {
                int errors = -99;
                memset(info.bits, 0xa5, sizeof(info.bits));
                memset(cw.bits, 0xa5, sizeof(cw.bits));
                assert(js8_ldpc_decode(llr, info.bits, cw.bits, &errors) == 0);
                assert(errors == (damaged ? 3 : 0));
                assert(memcmp(info.bits, expected_info, JS8_INFO_BITS) == 0);
                assert(memcmp(cw.bits, expected_cw, JS8_CODEWORD_BITS) == 0);
                assert(js8_crc12_check(info.bits) == 1);
                assert(memcmp(info.bits, payload, JS8_PAYLOAD_BITS) == 0);
            }
        }
        for (unsigned i = JS8_PAYLOAD_BITS; i < JS8_INFO_BITS; ++i) {
            info.bits[i] ^= 1;
            assert(js8_crc12_check(info.bits) == 0);
            info.bits[i] ^= 1;
        }
        assert(info.before == 0xa5 && info.after == 0xa5);
        assert(cw.before == 0xa5 && cw.after == 0xa5);
    }
}

static void invalid_tests(void)
{
    uint8_t payload[JS8_PAYLOAD_BITS] = {0}, input[JS8_INFO_BITS] = {0};
    uint8_t info[JS8_INFO_BITS], cw[JS8_CODEWORD_BITS];
    float llr[JS8_CODEWORD_BITS] = {0};
    memset(info, 0xa5, sizeof(info));
    memset(cw, 0xa5, sizeof(cw));
    int errors = 123;
    assert(js8_crc12_append(NULL, info) == -1);
    assert(js8_crc12_append(payload, NULL) == -1);
    assert(js8_crc12_check(NULL) == -1);
    assert(js8_ldpc_encode(NULL, cw) == -1);
    assert(js8_ldpc_encode(input, NULL) == -1);
    assert(js8_ldpc_decode(NULL, info, cw, &errors) == -1);
    assert(js8_ldpc_decode(llr, NULL, cw, &errors) == -1);
    assert(js8_ldpc_decode(llr, info, NULL, &errors) == -1);
    assert(js8_ldpc_decode(llr, info, cw, NULL) == -1);
    for (unsigned i = 0; i < JS8_INFO_BITS; ++i) {
        input[i] = 2;
        assert(js8_crc12_check(input) == -1);
        assert(js8_ldpc_encode(input, cw) == -1);
        if (i < JS8_PAYLOAD_BITS)
            assert(js8_crc12_append(input, info) == -1);
        input[i] = 0;
    }
    const float invalid[] = {NAN, INFINITY, -INFINITY};
    for (unsigned i = 0; i < 3; ++i) {
        llr[173] = invalid[i];
        assert(js8_ldpc_decode(llr, info, cw, &errors) == -1);
    }
    assert(errors == 123);
    for (unsigned i = 0; i < JS8_INFO_BITS; ++i) assert(info[i] == 0xa5);
    for (unsigned i = 0; i < JS8_CODEWORD_BITS; ++i) assert(cw[i] == 0xa5);
}

static void failure_tests(void)
{
    float llr[JS8_CODEWORD_BITS];
    uint8_t info[JS8_INFO_BITS], cw[JS8_CODEWORD_BITS], first[JS8_CODEWORD_BITS];
    uint32_t state = 0x12345678u;
    for (unsigned i = 0; i < JS8_CODEWORD_BITS; ++i) {
        state = state * 1664525u + 1013904223u;
        llr[i] = (state >> 31) ? 1.0f : -1.0f;
    }
    for (unsigned repeat = 0; repeat < 2; ++repeat) {
        int errors = 123;
        memset(info, 0xa5, sizeof(info));
        memset(cw, 0xa5, sizeof(cw));
        assert(js8_ldpc_decode(llr, info, cw, &errors) == 1);
        assert(errors == -1);
        for (unsigned i = 0; i < JS8_INFO_BITS; ++i) assert(info[i] == 0);
        for (unsigned i = 0; i < JS8_CODEWORD_BITS; ++i) assert(cw[i] <= 1);
        if (!repeat) memcpy(first, cw, sizeof(cw));
        else assert(memcmp(first, cw, sizeof(cw)) == 0);
    }
    /* LDPC alone accepts the zero word; CRC remains a separate gate. */
    memset(llr, 0, sizeof(llr));
    int errors;
    assert(js8_ldpc_decode(llr, info, cw, &errors) == 0);
    assert(errors == 0 && js8_crc12_check(info) == 0);
}

static void channel_tests(void)
{
    _Static_assert(JS8_TONE_COUNT == 79, "Normal frame length");
    _Static_assert(JS8_DATA_TONE_COUNT == 58, "Normal data tones");
    _Static_assert(JS8_SYMBOL_PERIOD_MS == 160, "Normal symbol duration");
    assert(JS8_TONE_SPACING_HZ == 6.25f);
    const uint8_t costas[] = {4, 2, 5, 6, 1, 3, 0};
    const unsigned sync_positions[] = {0, 36, 72};
    unsigned seen_words = 0;

    /* Interleave different payloads before repeating to expose retained state. */
    for (unsigned repeat = 0; repeat < 2; ++repeat) {
        for (unsigned v = 0; v < sizeof(vectors) / sizeof(vectors[0]); ++v) {
            uint8_t payload[JS8_PAYLOAD_BITS], codeword[JS8_CODEWORD_BITS];
            struct { uint8_t before, tones[JS8_TONE_COUNT], after; } output;
            memset(&output, 0xa5, sizeof(output));
            unpack(vectors[v].payload, payload, JS8_PAYLOAD_BITS);
            unpack(vectors[v].codeword, codeword, JS8_CODEWORD_BITS);
            assert(js8_channel_encode(payload, output.tones) == 0);
            assert(memcmp(output.tones, vectors[v].tones, JS8_TONE_COUNT) == 0);
            for (unsigned i = 0; i < JS8_TONE_COUNT; ++i)
                assert(output.tones[i] <= 7);
            for (unsigned i = 0; i < 3; ++i)
                assert(memcmp(output.tones + sync_positions[i], costas, 7) == 0);
            for (unsigned word = 0; word < JS8_DATA_TONE_COUNT; ++word) {
                unsigned bit = word * 3;
                unsigned direct = 4u * codeword[bit] + 2u * codeword[bit + 1]
                                  + codeword[bit + 2];
                unsigned position = word < 29 ? 7 + word : 43 + word - 29;
                assert(output.tones[position] == direct);
                seen_words |= 1u << direct;
            }
            assert(output.before == 0xa5 && output.after == 0xa5);
            for (unsigned i = 0; i < JS8_PAYLOAD_BITS; ++i)
                assert(payload[i] == (uint8_t)(vectors[v].payload[i] - '0'));
        }
    }
    /* All eight words are exercised, so a nonidentity/FT8 Gray map fails. */
    assert(seen_words == 0xffu);

    uint8_t payload[JS8_PAYLOAD_BITS] = {0};
    struct { uint8_t before, tones[JS8_TONE_COUNT], after; } output;
    memset(&output, 0xa5, sizeof(output));
    assert(js8_channel_encode(NULL, output.tones) == -1);
    assert(js8_channel_encode(payload, NULL) == -1);
    assert(js8_channel_encode(NULL, NULL) == -1);
    for (unsigned i = 0; i < JS8_PAYLOAD_BITS; ++i) {
        for (unsigned value = 2; value <= 255; ++value) {
            payload[i] = (uint8_t)value;
            assert(js8_channel_encode(payload, output.tones) == -1);
            for (unsigned j = 0; j < JS8_TONE_COUNT; ++j)
                assert(output.tones[j] == 0xa5);
            assert(output.before == 0xa5 && output.after == 0xa5);
        }
        payload[i] = 0;
    }
    assert(js8_channel_encode(payload, output.tones) == 0);
    assert(memcmp(output.tones, vectors[0].tones, JS8_TONE_COUNT) == 0);
    assert(output.before == 0xa5 && output.after == 0xa5);
}

int main(void)
{
    golden_tests();
    invalid_tests();
    failure_tests();
    channel_tests();
    puts("js8_phy_test: PASS");
    return 0;
}

#include "js8_directed.h"
#include "js8_channel.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

struct call28_vector { uint32_t packed; const char *plain, *portable; };
struct directed_vector {
    const char *payload, *from, *to;
    uint32_t from_packed, to_packed;
    unsigned portable_from, portable_to, command;
    int has_number, number;
    const char *diagnostic;
};
#include "js8_directed_vectors.h"
#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

static void unpack(const char *text, uint8_t bits[75])
{
    assert(strlen(text) == 75);
    for (unsigned i = 0; i < 75; ++i) bits[i] = (uint8_t)(text[i] - '0');
}

static void set_bits(uint8_t *bits, unsigned count, uint32_t value)
{
    for (unsigned i = 0; i < count; ++i) bits[i] = (value >> (count-1-i)) & 1;
}

static void unchanged(const void *out, size_t size)
{
    const unsigned char *p = out;
    for (size_t i = 0; i < size; ++i) assert(p[i] == 0xa5);
}

static void reject(const uint8_t *bits)
{
    Js8DirectedFrame frame;
    memset(&frame, 0xa5, sizeof(frame));
    assert(js8_directed_decode(bits, &frame) == -1);
    unchanged(&frame, sizeof(frame));
}

int main(int argc, char **argv)
{
    uint8_t bits[75];
    /* Host waveform fixtures use the existing PHY encoder, not an application
     * packer. Expected payload/content is independently pinned in the header.
     */
    if (argc == 2 && !strcmp(argv[1], "--waveform-fixtures")) {
        for (unsigned i = 0; i < COUNT(directed_vectors); ++i) {
            uint8_t tones[JS8_TONE_COUNT];
            unpack(directed_vectors[i].payload, bits);
            assert(js8_channel_encode(bits, tones) == 0);
            for (unsigned j = 0; j < JS8_TONE_COUNT; ++j) printf("%u", tones[j]);
            printf("\t%s\t%s\n", directed_vectors[i].payload, directed_vectors[i].diagnostic);
        }
        return 0;
    }
    assert(argc == 1);
    char call[JS8_CALLSIGN28_SIZE];
    for (unsigned i = 0; i < COUNT(call28_vectors); ++i) {
        for (int portable = 0; portable < 2; ++portable) {
            memset(call, 0xa5, sizeof(call));
            assert(js8_callsign28_unpack(call28_vectors[i].packed, portable, call) == 0);
            assert(!strcmp(call, portable ? call28_vectors[i].portable : call28_vectors[i].plain));
        }
    }
    memset(call, 0xa5, sizeof(call));
    assert(js8_callsign28_unpack(UINT32_C(1) << 28, 0, call) == -1);
    unchanged(call, sizeof(call));
    assert(js8_callsign28_unpack(UINT32_MAX, 1, call) == -1);
    unchanged(call, sizeof(call));
    assert(js8_callsign28_unpack(0, 0, NULL) == -1);
    for (unsigned code = 0; code < 32; ++code)
        assert(!strcmp(js8_directed_command_name((uint8_t)code), expected_commands[code]));
    for (unsigned code = 32; code < 256; ++code)
        assert(!strcmp(js8_directed_command_name((uint8_t)code), "INVALID"));

    Js8DirectedFrame frame;
    for (unsigned i = 0; i < COUNT(directed_vectors); ++i) {
        const struct directed_vector *v = &directed_vectors[i];
        unpack(v->payload, bits);
        for (unsigned tx = 0; tx < 8; ++tx) {
            set_bits(bits + 72, 3, tx);
            uint8_t saved[75]; memcpy(saved, bits, sizeof(bits));
            assert(js8_directed_decode(bits, &frame) == 0);
            assert(!strcmp(frame.from, v->from) && !strcmp(frame.to, v->to));
            assert(frame.from_packed == v->from_packed && frame.to_packed == v->to_packed);
            assert(frame.portable_from == v->portable_from && frame.portable_to == v->portable_to);
            assert(frame.command_code == v->command);
            assert(frame.has_number == v->has_number && frame.number == v->number);
            assert(frame.is_free_text == (v->command == 31));
            assert(frame.is_ack == (v->command == 14));
            assert(frame.is_73 == (v->command == 28));
            assert(frame.is_snr == (v->command == 25 || v->command == 29));
            assert(!memcmp(saved, bits, sizeof(bits)));
        }
    }
    /* Full command/number/portable cross-product, independent of TX flags. */
    unpack(directed_vectors[0].payload, bits);
    for (unsigned command = 0; command < 32; ++command) {
        set_bits(bits + 59, 5, command);
        for (unsigned number = 0; number < 64; ++number) {
            set_bits(bits + 66, 6, number);
            for (unsigned portable = 0; portable < 4; ++portable) {
                set_bits(bits + 64, 2, portable);
                assert(js8_directed_decode(bits, &frame) == 0);
                assert(frame.command_code == command);
                assert(frame.has_number == (number != 0));
                assert(frame.number == (number ? (int)number - 31 : 0));
                assert(frame.portable_from == (portable >> 1) && frame.portable_to == (portable & 1));
                assert(!strcmp(frame.from, portable & 2 ? "AG6AQ/P" : "AG6AQ"));
                assert(!strcmp(frame.to, portable & 1 ? "K1ABC/P" : "K1ABC"));
                assert(frame.is_free_text == (command == 31) && frame.is_ack == (command == 14));
                assert(frame.is_73 == (command == 28));
                assert(frame.is_snr == (command == 25 || command == 29));
            }
        }
    }
    /* All special names also travel through both directed fields with /P bits. */
    for (uint32_t value = 262177561; value <= 262177614; ++value) {
        set_bits(bits + 3, 28, value);
        set_bits(bits + 31, 28, value);
        assert(js8_directed_decode(bits, &frame) == 0);
        assert(js8_callsign28_unpack(value, 0, call) == 0);
        assert(!strcmp(frame.from, call) && !strcmp(frame.to, call));
        assert(frame.portable_from && frame.portable_to);
    }
    char snr[4];
    for (int number = -60; number <= 60; ++number) {
        char expected[8];
        snprintf(expected, sizeof(expected), "%+03d", number);
        assert(js8_directed_format_snr(number, snr) == 0 && !strcmp(snr, expected));
    }
    const int invalid_snr[] = {INT_MIN, -61, 61, INT_MAX};
    for (unsigned i = 0; i < COUNT(invalid_snr); ++i)
        assert(js8_directed_format_snr(invalid_snr[i], snr) == 0 && snr[0] == '\0');
    assert(js8_directed_format_snr(0, NULL) == -1);

    for (unsigned prefix = 0; prefix < 8; ++prefix) {
        if (prefix == 3) continue;
        set_bits(bits, 3, prefix);
        reject(bits);
    }
    reject(NULL);
    assert(js8_directed_decode(bits, NULL) == -1);
    unpack(directed_vectors[0].payload, bits);
    for (unsigned i = 0; i < 75; ++i) {
        uint8_t saved = bits[i];
        for (unsigned bad = 2; bad < 256; ++bad) {
            bits[i] = (uint8_t)bad;
            reject(bits);
        }
        bits[i] = saved;
    }
    puts("js8_directed_test: PASS");
    return 0;
}

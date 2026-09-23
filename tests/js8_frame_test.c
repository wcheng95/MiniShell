#include "js8_frame.h"
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

int main(void)
{
    static const char *const expected[] = {"000000000000", "LLLLLLLLLLLL", "CVUJtH2w2sAS"};
    static const uint8_t types[] = {0, 2, 3};
    uint8_t bits[75] = {0};
    struct { uint8_t before; Js8PhysicalFrame frame; uint8_t after; } output;
    for (unsigned v = 0; v < 3; ++v) {
        for (unsigned i = 0; i < 75; ++i) bits[i] = vectors[v].payload[i] - '0';
        memset(&output, 0xa5, sizeof(output));
        assert(js8_frame_unpack(bits, &output.frame) == 0);
        assert(!strcmp(output.frame.text12, expected[v]));
        assert(output.frame.type == types[v]);
        assert(output.before == 0xa5 && output.after == 0xa5);
    }
    const char alphabet[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz-+";
    for (unsigned word = 0; word < 64; ++word) {
        for (unsigned i = 0; i < 72; ++i) bits[i] = (word >> (5 - i % 6)) & 1;
        for (unsigned type = 0; type < 8; ++type) {
            for (unsigned i = 72; i < 75; ++i) bits[i] = (type >> (74 - i)) & 1;
            assert(js8_frame_unpack(bits, &output.frame) == 0);
            for (unsigned i = 0; i < 12; ++i) assert(output.frame.text12[i] == alphabet[word]);
            assert(output.frame.text12[12] == 0 && output.frame.type == type);
        }
    }
    memset(&output, 0xa5, sizeof(output));
    assert(js8_frame_unpack(NULL, &output.frame) == -1);
    assert(js8_frame_unpack(bits, NULL) == -1);
    for (unsigned i = 0; i < 75; ++i) {
        uint8_t saved = bits[i]; bits[i] = 2;
        assert(js8_frame_unpack(bits, &output.frame) == -1);
        const unsigned char *p = (const unsigned char *)&output;
        for (size_t j = 0; j < sizeof(output); ++j) assert(p[j] == 0xa5);
        bits[i] = saved;
    }
    puts("js8_frame_test: PASS");
    return 0;
}

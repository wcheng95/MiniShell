#include "js8_huffman.h"
#include "js8_channel.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

struct huff_entry { const char *text, *code; };
struct huff_vector {
    const char *input, *application;
    int consumed;
    const char *expected;
    unsigned count;
    const char *payload;
};
#include "js8_huffman_vectors.h"
#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

static void unpack(const char *text, uint8_t bits[75])
{
    assert(strlen(text) == 75);
    for (unsigned i = 0; i < 75; ++i) bits[i] = (uint8_t)(text[i] - '0');
}

/* Test framing only, with fixed independently sourced Huffman bits. */
static void frame(const char *content, uint8_t bits[75])
{
    size_t n = strlen(content);
    assert(n <= 69);
    memset(bits, 1, 75);
    bits[1] = 0;
    for (unsigned i = 0; i < n; ++i) bits[2+i] = (uint8_t)(content[i] - '0');
    bits[n+2] = 0;
}

static void unchanged(const void *out, size_t size)
{
    const unsigned char *p = out;
    for (size_t i = 0; i < size; ++i) assert(p[i] == 0xa5);
}

static void reject(const uint8_t *bits, Js8HuffmanStatus expected)
{
    Js8HuffmanData data;
    memset(&data, 0xa5, sizeof(data));
    assert(js8_huffman_data_decode(bits, &data) == expected);
    unchanged(&data, sizeof(data));
}

int main(int argc, char **argv)
{
    uint8_t bits[75];
    if (argc == 2 && !strcmp(argv[1], "--waveform-fixtures")) {
        for (unsigned i = 0; i < COUNT(huff_vectors); ++i) {
            uint8_t tones[JS8_TONE_COUNT];
            unpack(huff_vectors[i].payload, bits);
            assert(js8_channel_encode(bits, tones) == 0);
            for (unsigned j = 0; j < JS8_TONE_COUNT; ++j) printf("%u", tones[j]);
            printf("\t%s\t%s\n", huff_vectors[i].payload, huff_vectors[i].expected);
        }
        return 0;
    }
    assert(argc == 1);
    Js8HuffmanData data;
    assert(COUNT(huff_entries) == 44);
    for (unsigned i = 0; i < COUNT(huff_entries); ++i) {
        const struct huff_entry *entry = &huff_entries[i];
        for (unsigned j = 0; j < COUNT(huff_entries); ++j)
            if (i != j) assert(strncmp(entry->code, huff_entries[j].code, strlen(entry->code)));
        frame(entry->code, bits);
        assert(js8_huffman_data_decode(bits, &data) == JS8_HUFF_OK);
        assert(!strcmp(data.text, entry->text) && data.text_len == 1);
        assert(data.encoded_bit_count == strlen(entry->code));
        /* Every proper code prefix is incomplete, both alone and after E. */
        for (unsigned n = 1; n < strlen(entry->code); ++n) {
            char content[12] = "100";
            memcpy(content+3, entry->code, n);
            content[3+n] = '\0';
            frame(content, bits);
            assert(js8_huffman_data_decode(bits, &data) == JS8_HUFF_OK);
            assert(!strcmp(data.text, "E") && data.encoded_bit_count == n+3);
            frame(content+3, bits);
            assert(js8_huffman_data_decode(bits, &data) == JS8_HUFF_OK);
            assert(data.text_len == 0 && data.text[0] == '\0');
        }
    }
    unsigned seen_prefix[2] = {0};
    for (unsigned i = 0; i < COUNT(huff_vectors); ++i) {
        const struct huff_vector *v = &huff_vectors[i];
        assert(strlen(v->application) == 72 && !strncmp(v->application, v->payload, 72));
        if (v->input) assert(v->consumed == (int)strlen(v->expected));
        unpack(v->payload, bits);
        seen_prefix[bits[2]] = 1;
        for (unsigned tx = 0; tx < 8; ++tx) {
            for (unsigned j = 0; j < 3; ++j) bits[72+j] = (tx >> (2-j)) & 1;
            uint8_t saved[75]; memcpy(saved, bits, sizeof(bits));
            assert(js8_huffman_data_decode(bits, &data) == JS8_HUFF_OK);
            assert(!strcmp(data.text, v->expected));
            assert(data.text_len == strlen(v->expected) && data.encoded_bit_count == v->count);
            assert(data.text_len < sizeof(data.text));
            assert(!memcmp(bits, saved, sizeof(bits)));
        }
    }
    assert(seen_prefix[0] && seen_prefix[1]);
    unpack(huff_vectors[8].payload, bits);
    assert(js8_huffman_data_decode(bits, &data) == JS8_HUFF_OK);
    assert(data.text_len == 34 && data.encoded_bit_count == 68);
    unpack(huff_vectors[9].payload, bits);
    assert(js8_huffman_data_decode(bits, &data) == JS8_HUFF_OK);
    assert(data.text_len == 23 && data.encoded_bit_count == 69 && bits[71] == 0);
    /* Every sentinel position is bounded, including empty content and one-bit
     * partial content. All-one content has no complete code until five bits.
     */
    for (unsigned sentinel = 2; sentinel < 72; ++sentinel) {
        memset(bits, 1, sizeof(bits)); bits[1] = 0; bits[sentinel] = 0;
        assert(js8_huffman_data_decode(bits, &data) == JS8_HUFF_OK);
        assert(data.encoded_bit_count == sentinel-2 && data.text_len == (sentinel-2)/5);
        for (unsigned j = 0; j < data.text_len; ++j) assert(data.text[j] == 'O');
    }
    memset(bits, 1, sizeof(bits)); bits[1] = 0;
    reject(bits, JS8_HUFF_BAD_PADDING);
    for (unsigned prefix = 0; prefix < 8; ++prefix) {
        if (prefix == 4 || prefix == 5) continue;
        for (unsigned j = 0; j < 3; ++j) bits[j] = (prefix >> (2-j)) & 1;
        reject(bits, prefix >= 6 ? JS8_HUFF_COMPRESSED : JS8_HUFF_WRONG_CLASS);
    }
    reject(NULL, JS8_HUFF_INVALID);
    assert(js8_huffman_data_decode(bits, NULL) == JS8_HUFF_INVALID);
    unpack(huff_vectors[0].payload, bits);
    for (unsigned i = 0; i < 75; ++i) {
        uint8_t saved = bits[i];
        for (unsigned bad = 2; bad < 256; ++bad) {
            bits[i] = (uint8_t)bad;
            reject(bits, JS8_HUFF_INVALID);
        }
        bits[i] = saved;
    }
    puts("js8_huffman_test: PASS");
    return 0;
}

#include "js8_jsc.h"
#include "js8_channel.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

struct jsc_word { uint32_t index; const char *text; };
struct jsc_vector { const char *payload, *text; unsigned bits, words; };
#include "js8_jsc_vectors.h"
#define COUNT(a) (sizeof(a)/sizeof((a)[0]))
#define SPAN (256u * 27u)
#define RESOURCE_BYTES (4128u + 1024u * SPAN)

typedef struct {
    uint8_t header[32];
    uint32_t fail_at, patch_at, offset_block, offset_value;
    uint8_t patch_value;
    const char *replacement;
    unsigned reads, max_read;
} Reader;
static void put32(uint8_t *p, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(value >> (8*i));
}
static Reader reader(void)
{
    Reader r = {0};
    memcpy(r.header, "JSC1", 4); r.header[4] = 1; r.header[7] = 1;
    put32(r.header+8, 262144); put32(r.header+12, 1024);
    put32(r.header+16, 32); put32(r.header+20, 4128); r.header[24] = 1;
    r.fail_at = r.patch_at = r.offset_block = UINT32_MAX;
    return r;
}
static const char *word_at(const Reader *r, uint32_t index)
{
    if (r->replacement) return r->replacement;
    for (unsigned i = 0; i < COUNT(jsc_words); ++i)
        if (jsc_words[i].index == index) return jsc_words[i].text;
    return "E";
}
/* Small in-memory virtual resource. One block is built locally on demand;
 * no real dictionary file or filesystem is needed by portable core tests.
 */
static int read_resource(void *context, uint32_t offset, void *dst, size_t bytes)
{
    Reader *r = context;
    assert(offset <= RESOURCE_BYTES && bytes <= RESOURCE_BYTES-offset);
    ++r->reads;
    if (bytes > r->max_read) r->max_read = (unsigned)bytes;
    if (r->fail_at >= offset && r->fail_at-offset < bytes) return -1;
    uint8_t *out = dst;
    if (offset < 32) {
        assert(bytes <= 32-offset); memcpy(out, r->header+offset, bytes);
    } else if (offset < 4128) {
        assert(bytes <= 4128-offset);
        for (unsigned i = 0; i < bytes; ++i) {
            uint32_t block = (offset+i-32)/4;
            uint32_t value = block == r->offset_block ? r->offset_value : 4128+block*SPAN;
            out[i] = (uint8_t)(value >> (8*((offset+i-32)%4)));
        }
    } else {
        uint32_t block = (offset-4128)/SPAN, within = (offset-4128)%SPAN;
        uint8_t data[SPAN] = {0};
        unsigned pos = 0;
        for (unsigned slot = 0; slot < 256; ++slot) {
            const char *word = word_at(r, block*256+slot);
            size_t n = strlen(word);
            assert(n <= 26);
            data[pos++] = (uint8_t)n;
            memcpy(data+pos, word, n); pos += (unsigned)n;
        }
        assert(bytes <= SPAN-within);
        memcpy(out, data+within, bytes);
    }
    if (r->patch_at >= offset && r->patch_at-offset < bytes)
        out[r->patch_at-offset] = r->patch_value;
    return 0;
}
static void unpack(const char *text, uint8_t bits[75])
{
    assert(strlen(text) == 75);
    for (unsigned i = 0; i < 75; ++i) bits[i] = (uint8_t)(text[i]-'0');
}
static void unchanged(const void *data, size_t bytes)
{
    const unsigned char *p = data;
    for (size_t i = 0; i < bytes; ++i) assert(p[i] == 0xa5);
}
static void bad_lookup(Js8JscDictionary *dict, uint32_t index, Js8JscStatus status)
{
    char text[27]; uint8_t length = 0xa5;
    memset(text, 0xa5, sizeof(text));
    assert(js8_jsc_dictionary_lookup(dict, index, text, &length) == status);
    unchanged(text, sizeof(text)); assert(length == 0xa5);
}
static void bad_decode(const uint8_t *bits, const Js8JscDictionary *dict, Js8JscStatus status)
{
    Js8JscData out; memset(&out, 0xa5, sizeof(out));
    assert(js8_jsc_data_decode(bits, dict, &out) == status);
    unchanged(&out, sizeof(out));
}
int main(int argc, char **argv)
{
    uint8_t bits[75];
    if (argc == 2 && !strcmp(argv[1], "--waveform-fixtures")) {
        for (unsigned i = 0; i < COUNT(jsc_vectors); ++i) {
            uint8_t tones[79]; unpack(jsc_vectors[i].payload, bits);
            assert(js8_channel_encode(bits, tones) == 0);
            for (unsigned j = 0; j < 79; ++j) printf("%u", tones[j]);
            printf("\t%s\t", jsc_vectors[i].payload);
            for (const unsigned char *p = (const unsigned char *)jsc_vectors[i].text; *p; ++p) printf("%02x", *p);
            putchar('\n');
        }
        return 0;
    }
    assert(argc == 1);
    Reader r = reader(); Js8JscDictionary dict;
    assert(js8_jsc_dictionary_init(read_resource, &r, RESOURCE_BYTES, &dict) == JS8_JSC_OK);
    for (unsigned i = 0; i < COUNT(jsc_words); ++i) {
        char text[27]; uint8_t length;
        assert(js8_jsc_dictionary_lookup(&dict, jsc_words[i].index, text, &length) == JS8_JSC_OK);
        assert(!strcmp(text, jsc_words[i].text) && length == strlen(text));
    }
    Js8JscData out;
    for (unsigned i = 0; i < COUNT(jsc_vectors); ++i) {
        const struct jsc_vector *v = &jsc_vectors[i]; unpack(v->payload, bits);
        for (unsigned tx = 0; tx < 8; ++tx) {
            for (unsigned b = 0; b < 3; ++b) bits[72+b] = (tx >> (2-b)) & 1;
            uint8_t saved[75]; memcpy(saved, bits, sizeof(bits));
            assert(js8_jsc_data_decode(bits, &dict, &out) == JS8_JSC_OK);
            assert(!strcmp(out.text, v->text) && out.text_len == strlen(v->text));
            assert(out.dictionary_words == v->words && out.encoded_bit_count == v->bits);
            assert(!memcmp(saved, bits, sizeof(bits)));
        }
    }
    assert(r.max_read <= 32);
    unsigned measured_max_read = r.max_read;
    Reader other = reader(); other.replacement = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    Js8JscDictionary second;
    assert(js8_jsc_dictionary_init(read_resource, &other, RESOURCE_BYTES, &second) == JS8_JSC_OK);
    unpack(jsc_vectors[15].payload, bits); /* 14 entries, 13 separators. */
    assert(js8_jsc_data_decode(bits, &second, &out) == JS8_JSC_OK);
    assert(out.dictionary_words == 14 && out.text_len == 14*26+13);
    unpack(jsc_vectors[0].payload, bits);
    assert(js8_jsc_data_decode(bits, &dict, &out) == JS8_JSC_OK && !strcmp(out.text, "E"));
    assert(js8_jsc_data_decode(bits, &second, &out) == JS8_JSC_OK && out.text_len == 26);
    for (unsigned pos = 0; pos < 32; ++pos) {
        Reader bad = reader(); bad.header[pos] ^= 0x80;
        Js8JscDictionary untouched; memset(&untouched, 0xa5, sizeof(untouched));
        assert(js8_jsc_dictionary_init(read_resource, &bad, RESOURCE_BYTES, &untouched) == JS8_JSC_BAD_RESOURCE);
        unchanged(&untouched, sizeof(untouched));
    }
    Js8JscDictionary untouched; memset(&untouched, 0xa5, sizeof(untouched));
    assert(js8_jsc_dictionary_init(read_resource, &r, 4127, &untouched) == JS8_JSC_BAD_RESOURCE);
    unchanged(&untouched, sizeof(untouched));
    r.fail_at = 10;
    assert(js8_jsc_dictionary_init(read_resource, &r, RESOURCE_BYTES, &untouched) == JS8_JSC_BAD_RESOURCE);
    unchanged(&untouched, sizeof(untouched));
    r.fail_at = 35; /* Truncated index callback. */
    bad_lookup(&dict, 0, JS8_JSC_BAD_RESOURCE);
    bad_decode(bits, &dict, JS8_JSC_BAD_RESOURCE);
    r.fail_at = UINT32_MAX;
    r.offset_block = 0;
    for (unsigned i = 0; i < 3; ++i) {
        const uint32_t offsets[] = {0, RESOURCE_BYTES, UINT32_MAX};
        r.offset_value = offsets[i]; bad_lookup(&dict, 0, JS8_JSC_BAD_RESOURCE);
    }
    r.offset_block = 1; r.offset_value = 4128;
    bad_lookup(&dict, 0, JS8_JSC_BAD_RESOURCE);
    r.offset_block = 1023; r.offset_value = RESOURCE_BYTES-1;
    r.patch_at = RESOURCE_BYTES-1; r.patch_value = 26;
    bad_lookup(&dict, 1023*256, JS8_JSC_BAD_RESOURCE);
    r = reader(); r.patch_at = 4128; r.patch_value = 27;
    bad_lookup(&dict, 0, JS8_JSC_BAD_RESOURCE);
    bad_lookup(&dict, 6, JS8_JSC_BAD_RESOURCE); /* Corrupt skipped record too. */
    r.patch_at = 4129; r.patch_value = 0;
    bad_lookup(&dict, 0, JS8_JSC_BAD_RESOURCE);
    r = reader(); r.fail_at = 4129;
    bad_lookup(&dict, 0, JS8_JSC_BAD_RESOURCE);
    bad_decode(bits, &dict, JS8_JSC_BAD_RESOURCE);
    r = reader(); bad_lookup(&dict, 262144, JS8_JSC_BAD_INDEX);
    bad_lookup(&dict, UINT32_MAX, JS8_JSC_BAD_INDEX);
    assert(js8_jsc_dictionary_init(NULL, &r, RESOURCE_BYTES, &dict) == JS8_JSC_INVALID);
    assert(js8_jsc_dictionary_init(read_resource, &r, RESOURCE_BYTES, NULL) == JS8_JSC_INVALID);
    char text[27]; uint8_t length;
    assert(js8_jsc_dictionary_lookup(&dict, 0, NULL, &length) == JS8_JSC_INVALID);
    assert(js8_jsc_dictionary_lookup(&dict, 0, text, NULL) == JS8_JSC_INVALID);
    bad_lookup(NULL, 0, JS8_JSC_BAD_RESOURCE);
    bad_decode(bits, NULL, JS8_JSC_BAD_RESOURCE);
    bad_decode(NULL, &dict, JS8_JSC_INVALID);
    assert(js8_jsc_data_decode(bits, &dict, NULL) == JS8_JSC_INVALID);
    for (unsigned prefix = 0; prefix < 6; ++prefix) {
        for (unsigned b = 0; b < 3; ++b) bits[b] = (prefix >> (2-b)) & 1;
        bad_decode(bits, &dict, JS8_JSC_WRONG_CLASS);
    }
    memset(bits, 1, sizeof(bits)); bad_decode(bits, &dict, JS8_JSC_BAD_PADDING);
    unpack(jsc_vectors[0].payload, bits);
    for (unsigned i = 0; i < 75; ++i) {
        uint8_t saved = bits[i];
        for (unsigned value = 2; value < 256; ++value) {
            bits[i] = (uint8_t)value; bad_decode(bits, &dict, JS8_JSC_INVALID);
        }
        bits[i] = saved;
    }
    printf("JSC resource state=%zu output=%zu, max read=%u; PASS\n", sizeof(dict), sizeof(out), measured_max_read);
    return 0;
}

#include "js8_jsc.h"

#include <string.h>

static uint16_t le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (uint16_t)p[1] << 8);
}
static uint32_t le32(const uint8_t *p)
{
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static int valid_dictionary(const Js8JscDictionary *dict)
{
    return dict && dict->read && dict->data_offset == 4128 &&
           dict->resource_bytes >= 4128 + JS8_JSC_ENTRIES;
}
static int read_at(const Js8JscDictionary *dict, uint32_t offset, void *dst, size_t bytes)
{
    return offset <= dict->resource_bytes && bytes <= dict->resource_bytes - offset &&
           dict->read(dict->context, offset, dst, bytes) == 0;
}
Js8JscStatus js8_jsc_dictionary_init(Js8JscReadFn read, void *context,
                                    uint32_t resource_bytes, Js8JscDictionary *out)
{
    if (!read || !out) return JS8_JSC_INVALID;
    Js8JscDictionary result = {read, context, resource_bytes, 4128};
    uint8_t header[32];
    if (!valid_dictionary(&result) || !read_at(&result, 0, header, sizeof(header)) ||
        memcmp(header, "JSC1", 4) || le16(header+4) != 1 || le16(header+6) != 256 ||
        le32(header+8) != JS8_JSC_ENTRIES || le32(header+12) != 1024 ||
        le32(header+16) != 32 || le32(header+20) != 4128 ||
        le32(header+24) != 1 || le32(header+28) != 0)
        return JS8_JSC_BAD_RESOURCE;
    *out = result;
    return JS8_JSC_OK;
}
Js8JscStatus js8_jsc_dictionary_lookup(const Js8JscDictionary *dict, uint32_t index,
                                      char out[JS8_JSC_ENTRY_MAX + 1], uint8_t *length)
{
    if (!out || !length) return JS8_JSC_INVALID;
    if (!valid_dictionary(dict)) return JS8_JSC_BAD_RESOURCE;
    if (index >= JS8_JSC_ENTRIES) return JS8_JSC_BAD_INDEX;
    uint32_t block = index / 256, slot = index % 256;
    uint8_t offsets[8];
    if (!read_at(dict, 32 + block*4, offsets, block == 1023 ? 4 : 8))
        return JS8_JSC_BAD_RESOURCE;
    uint32_t pos = le32(offsets);
    uint32_t end = block == 1023 ? dict->resource_bytes : le32(offsets+4);
    if (pos < dict->data_offset || end > dict->resource_bytes || pos >= end ||
        (block == 0 && pos != dict->data_offset))
        return JS8_JSC_BAD_RESOURCE;
    for (uint32_t i = 0; i <= slot; ++i) {
        uint8_t size;
        if (pos >= end || !read_at(dict, pos, &size, 1)) return JS8_JSC_BAD_RESOURCE;
        ++pos;
        if (size > JS8_JSC_ENTRY_MAX || size > end - pos) return JS8_JSC_BAD_RESOURCE;
        if (i == slot) {
            char word[JS8_JSC_ENTRY_MAX + 1];
            if (!read_at(dict, pos, word, size) || memchr(word, '\0', size))
                return JS8_JSC_BAD_RESOURCE;
            word[size] = '\0';
            memcpy(out, word, size+1);
            *length = size;
            return JS8_JSC_OK;
        }
        pos += size;
    }
    return JS8_JSC_BAD_RESOURCE;
}

/* At most 14 terminals fit: 13*(4+1) + 4 = 69. Each adds <=26+1 bytes. */
_Static_assert(JS8_JSC_TEXT_SIZE >= 14 * (JS8_JSC_ENTRY_MAX + 1) + 1,
               "JSC fragment plus spaces and NUL must fit");
Js8JscStatus js8_jsc_data_decode(const uint8_t bits[JS8_PAYLOAD_BITS],
                                const Js8JscDictionary *dict, Js8JscData *out)
{
    Js8ProtocolEnvelope envelope;
    if (!out || js8_protocol_envelope_decode(bits, &envelope)) return JS8_JSC_INVALID;
    if (envelope.app_class != JS8_APP_FRAME_DATA_COMPRESSED) return JS8_JSC_WRONG_CLASS;
    if (!valid_dictionary(dict)) return JS8_JSC_BAD_RESOURCE;
    int sentinel = 71;
    while (sentinel >= 2 && bits[sentinel]) --sentinel;
    if (sentinel < 2) return JS8_JSC_BAD_PADDING;
    Js8JscData result = {0};
    result.encoded_bit_count = (uint8_t)(sentinel-2);
    unsigned cursor = 2, k = 0;
    uint32_t j = 0;
    static const uint32_t base[] = {0, 7, 70, 637, 5740, 51667, 465010, 4185097};
    while (cursor + 4 <= (unsigned)sentinel) {
        unsigned nibble = 0;
        for (unsigned b = 0; b < 4; ++b) nibble = (nibble << 1) | bits[cursor++];
        if (nibble >= 7) {
            /* Six leading nibbles already imply an index beyond this map.
             * Stop before any arithmetic overflow or base-array overrun.
             */
            if (++k >= 6) break;
            j = j*9 + nibble-7;
            continue;
        }
        j = j*7 + nibble + base[k];
        if (j >= JS8_JSC_ENTRIES) break;
        unsigned separator = cursor < (unsigned)sentinel ? bits[cursor++] : 0;
        char word[JS8_JSC_ENTRY_MAX + 1];
        uint8_t size;
        Js8JscStatus status = js8_jsc_dictionary_lookup(dict, j, word, &size);
        if (status != JS8_JSC_OK) return status;
        if ((size_t)result.text_len + size + separator >= sizeof(result.text))
            return JS8_JSC_OUTPUT_FULL;
        memcpy(result.text + result.text_len, word, size);
        result.text_len += size;
        if (separator) result.text[result.text_len++] = ' ';
        ++result.dictionary_words;
        j = 0; k = 0;
    }
    result.text[result.text_len] = '\0';
    *out = result;
    return JS8_JSC_OK;
}

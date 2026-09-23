#include "js8_huffman.h"

/* Exact v3.0.3 Varicode.cpp hufftable; see tests/js8_huffman_vectors.md. */
static const struct { char symbol; uint8_t code, length; } codes[] = {
    {' ', 1, 2},
    {'E', 4, 3},
    {'T', 13, 4},
    {'A', 3, 4},
    {'O', 31, 5},
    {'I', 28, 5},
    {'N', 23, 5},
    {'S', 20, 5},
    {'H', 3, 5},
    {'R', 0, 5},
    {'D', 59, 6},
    {'L', 51, 6},
    {'C', 49, 6},
    {'U', 45, 6},
    {'M', 43, 6},
    {'W', 11, 6},
    {'F', 9, 6},
    {'G', 5, 6},
    {'Y', 3, 6},
    {'P', 123, 7},
    {'B', 121, 7},
    {'.', 116, 7},
    {'V', 101, 7},
    {'K', 100, 7},
    {'-', 97, 7},
    {'+', 96, 7},
    {'?', 89, 7},
    {'!', 88, 7},
    {'"', 85, 7},
    {'X', 84, 7},
    {'0', 21, 7},
    {'J', 20, 7},
    {'1', 17, 7},
    {'Q', 16, 7},
    {'2', 9, 7},
    {'Z', 8, 7},
    {'3', 5, 7},
    {'5', 4, 7},
    {'4', 245, 8},
    {'9', 244, 8},
    {'8', 241, 8},
    {'6', 240, 8},
    {'7', 235, 8},
    {'/', 234, 8},
};

_Static_assert(JS8_HUFF_TEXT_MAX >= 69 / 2 + 1, "Huffman text plus NUL must fit");

Js8HuffmanStatus js8_huffman_data_decode(const uint8_t bits[JS8_PAYLOAD_BITS],
                                        Js8HuffmanData *out)
{
    Js8ProtocolEnvelope envelope;
    if (!out || js8_protocol_envelope_decode(bits, &envelope))
        return JS8_HUFF_INVALID;
    if (envelope.app_class == JS8_APP_FRAME_DATA_COMPRESSED)
        return JS8_HUFF_COMPRESSED;
    if (envelope.app_class != JS8_APP_FRAME_DATA || bits[0] != 1 || bits[1] != 0)
        return JS8_HUFF_WRONG_CLASS;
    int sentinel = 71;
    while (sentinel >= 2 && bits[sentinel]) --sentinel;
    /* Never let the selector bit stand in for a missing padding sentinel.
     * Qt mid(1, -1) would instead expose all 70 bits in this malformed case.
     */
    if (sentinel < 2)
        return JS8_HUFF_BAD_PADDING;
    Js8HuffmanData result = {0};
    result.encoded_bit_count = (uint8_t)(sentinel - 2);
    unsigned cursor = 2;
    while (cursor < (unsigned)sentinel) {
        unsigned i;
        for (i = 0; i < sizeof(codes) / sizeof(codes[0]); ++i) {
            if (cursor + codes[i].length > (unsigned)sentinel) continue;
            unsigned code = 0;
            for (unsigned j = 0; j < codes[i].length; ++j)
                code = (code << 1) | bits[cursor + j];
            if (code != codes[i].code) continue;
            result.text[result.text_len++] = codes[i].symbol;
            cursor += codes[i].length;
            break;
        }
        if (i == sizeof(codes) / sizeof(codes[0])) break;
    }
    result.text[result.text_len] = '\0';
    *out = result;
    return JS8_HUFF_OK;
}

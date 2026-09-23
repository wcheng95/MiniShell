#ifndef JS8_HUFFMAN_H
#define JS8_HUFFMAN_H

#include "js8_protocol_frame.h"

/* At most 69 content bits / shortest 2-bit code = 34 characters, plus NUL. */
#define JS8_HUFF_TEXT_MAX 35

typedef struct {
    char text[JS8_HUFF_TEXT_MAX];
    uint8_t text_len;
    uint8_t encoded_bit_count; /* Unpadded content, including an incomplete suffix. */
} Js8HuffmanData;

typedef enum {
    JS8_HUFF_OK = 0,
    JS8_HUFF_INVALID = -1,
    JS8_HUFF_WRONG_CLASS = -2,
    JS8_HUFF_COMPRESSED = -3,
    JS8_HUFF_BAD_PADDING = -4
} Js8HuffmanStatus;

/* Normal 10 DATA only; all 75 input bits must be 0/1. Transmission flags are
 * validated but not decoded as content. Missing sentinel in bits 2..71 is an
 * error; an incomplete final Huffman code returns the decoded prefix, like
 * upstream. Output is unchanged on any error. Input/output must not overlap.
 * No CRC validation, JSC, directed association, or reassembly.
 */
Js8HuffmanStatus js8_huffman_data_decode(const uint8_t bits[JS8_PAYLOAD_BITS],
                                        Js8HuffmanData *out);

#endif

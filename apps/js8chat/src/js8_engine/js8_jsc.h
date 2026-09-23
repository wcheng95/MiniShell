#ifndef JS8_JSC_H
#define JS8_JSC_H

#include "js8_protocol_frame.h"
#include <stddef.h>

#define JS8_JSC_ENTRY_MAX 26u
#define JS8_JSC_TEXT_SIZE 384u
#define JS8_JSC_ENTRIES 262144u

/* Return 0 only after copying exactly bytes bytes. Resource/context must remain
 * valid and immutable while the dictionary is used; caller owns all I/O.
 */
typedef int (*Js8JscReadFn)(void *context, uint32_t offset, void *dst, size_t bytes);
typedef struct {
    Js8JscReadFn read;
    void *context;
    uint32_t resource_bytes;
    uint32_t data_offset;
} Js8JscDictionary;

typedef enum {
    JS8_JSC_OK = 0,
    JS8_JSC_INVALID = -1,
    JS8_JSC_WRONG_CLASS = -2,
    JS8_JSC_BAD_PADDING = -3,
    JS8_JSC_BAD_RESOURCE = -4,
    JS8_JSC_BAD_INDEX = -5,
    JS8_JSC_OUTPUT_FULL = -6
} Js8JscStatus;

typedef struct {
    char text[JS8_JSC_TEXT_SIZE]; /* Latin-1, not UTF-8. */
    uint16_t text_len;
    uint8_t encoded_bit_count;
    uint8_t dictionary_words;
} Js8JscData;

/* All errors leave output(s) unchanged. No heap or resource ownership transfer.
 * JSC1 v1 header is validated at init; touched offsets/records at lookup.
 * SHA-256 identity belongs to caller/build tooling, not this structural reader.
 */
Js8JscStatus js8_jsc_dictionary_init(Js8JscReadFn read, void *context,
                                    uint32_t resource_bytes, Js8JscDictionary *out);
Js8JscStatus js8_jsc_dictionary_lookup(const Js8JscDictionary *dict, uint32_t index,
                                      char out[JS8_JSC_ENTRY_MAX + 1], uint8_t *length);
/* All 75 bits validated; only 11 Normal content is decoded. Partial/out-of-range
 * wire codewords stop like upstream and return the valid prefix. Resource errors
 * fail the entire operation. Eight or more continuation nibbles stop safely
 * rather than indexing upstream's base[8] out of bounds. No CRC or reassembly.
 * Input/dictionary/output storage must not overlap.
 */
Js8JscStatus js8_jsc_data_decode(const uint8_t bits[JS8_PAYLOAD_BITS],
                                const Js8JscDictionary *dict, Js8JscData *out);

#endif

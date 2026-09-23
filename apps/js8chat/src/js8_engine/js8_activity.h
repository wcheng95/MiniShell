#ifndef JS8_ACTIVITY_H
#define JS8_ACTIVITY_H

#include <stddef.h>
#include <stdint.h>
#include "js8_compound.h"
#include "js8_directed.h"
#include "js8_huffman.h"
#include "js8_jsc.h"
#include "js8_reassembly.h"

typedef enum {
    JS8_ACTIVITY_HEARTBEAT, JS8_ACTIVITY_CQ, JS8_ACTIVITY_COMPOUND,
    JS8_ACTIVITY_DIRECTED, JS8_ACTIVITY_DATA, JS8_ACTIVITY_MESSAGE
} Js8ActivityKind;
typedef enum { JS8_ACTIVITY_CODEC_NONE, JS8_ACTIVITY_CODEC_HUFFMAN,
               JS8_ACTIVITY_CODEC_JSC } Js8ActivityCodec;

/* Already-decoded facts, not wire bits. Identity strings are supplied by the
 * caller (canonical command/beacon names from its content decoder).
 */
typedef struct {
    Js8ActivityKind kind;
    uint32_t slot_index;
    int32_t frequency_millihz;
    uint8_t tx_flags;
    int score, hard_errors;
    char call[sizeof(((Js8CompoundFields *)0)->callsign)];
    char from[JS8_CALLSIGN28_SIZE], to[JS8_CALLSIGN28_SIZE];
    char grid[sizeof(((Js8BeaconFrame *)0)->grid)];
    char command[16], beacon[12];
    uint8_t command_code, subtype, bits3;
    uint16_t extra;
    uint8_t compound_directed, has_number, free_text, ack, end73;
    int number;
    Js8ActivityCodec codec;
    uint32_t first_slot, last_slot;
} Js8ActivityFields;

typedef struct {
    Js8ActivityFields fields;
    uint64_t elapsed_seconds;
    uint16_t text_len;
    char text[JS8_RX_MESSAGE_SIZE]; /* Bytes, including embedded NUL/Latin-1. */
} Js8Activity;

/* Bounded owned snapshot; no decode, lookup, JSON, clock or I/O. All pointers
 * required except text for zero length. No overlapping objects. Validates only
 * fields relevant to kind; callers zero unused fields. Copies exactly text_len
 * bytes and adds NUL. DATA capacities follow its codec; MESSAGE allows 1023
 * bytes. Other kinds require zero text length. elapsed_seconds = slot * 15 in
 * uint64_t. Returns 0 or -1; invalid input leaves output unchanged.
 */
int js8_activity_build(const Js8ActivityFields *fields, const char *text,
                       size_t text_len, Js8Activity *out);
#endif

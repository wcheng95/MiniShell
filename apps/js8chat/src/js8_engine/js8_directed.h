#ifndef JS8_DIRECTED_H
#define JS8_DIRECTED_H

#include "js8_protocol_frame.h"

/* Longest special name is @RESERVE/0 (10 chars); generic transforms + /P fit. */
#define JS8_CALLSIGN28_SIZE 11

typedef struct {
    char from[JS8_CALLSIGN28_SIZE];
    char to[JS8_CALLSIGN28_SIZE];
    uint32_t from_packed;
    uint32_t to_packed;
    uint8_t command_code;
    uint8_t portable_from;
    uint8_t portable_to;
    int has_number;
    int number; /* Zero when absent; otherwise number6 - 31, including +32. */
    int is_free_text;
    int is_ack;
    int is_73;
    int is_snr;
} Js8DirectedFrame;

/* Every 28-bit value is supported. Nonzero portable appends /P to generic
 * callsigns only. Return -1 for null output or wider input, without writes.
 */
int js8_callsign28_unpack(uint32_t packed, int portable, char out[JS8_CALLSIGN28_SIZE]);

/* Canonical v3.0.3 QMap::key name, including spaces; invalid code -> "INVALID". */
const char *js8_directed_command_name(uint8_t code);

/* v3.0.3 formatSNR: signed two digits for -60..60, empty string otherwise.
 * Four-byte caller storage; returns -1 only for null output, 0 otherwise.
 */
int js8_directed_format_snr(int number, char out[4]);

/* Validates all 75 unpacked 0/1 bits and requires the DIRECTED envelope.
 * Returns 0 on success, -1 on invalid input/class with output unchanged.
 * Input/output must not overlap. TX flags do not affect content decoding.
 * No CRC, compound association, command actions, or continuation decoding.
 */
int js8_directed_decode(const uint8_t bits[JS8_PAYLOAD_BITS], Js8DirectedFrame *out);

#endif

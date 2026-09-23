#ifndef JS8_COMPOUND_H
#define JS8_COMPOUND_H

#include "js8_protocol_frame.h"

typedef struct {
    Js8AppFrameClass app_class;
    char callsign[12];
    uint16_t extra16;
    uint8_t bits3;
} Js8CompoundFields;

typedef struct {
    char callsign[12];
    char grid[5];
    int has_grid;
    int is_cq;
    uint8_t subtype;
} Js8BeaconFrame;

typedef struct {
    char callsign[12];
    char grid[5];
    int has_grid;
    uint16_t extra16;
    uint8_t bits3;
} Js8CompoundIdentity;

/* Fixed-size caller buffers. Return 0 on success, -1 for null output or a
 * callsign value wider than 50 bits. No callsign validity filtering is done.
 * Grid values >32400 succeed with an empty string, including reserved values.
 * Invalid arguments leave output unchanged.
 */
int js8_callsign50_unpack(uint64_t packed, char out[12]);
int js8_grid_unpack(uint16_t packed, char out[5]);

/* All 75 bits must be 0/1. Input/output must not overlap. Return 0 on success,
 * -1 for invalid input or unsupported class, leaving output unchanged.
 * Transmission flags are validated but do not affect content decoding.
 * Raw fields accept HEARTBEAT, COMPOUND, COMPOUND_DIRECTED only; semantic
 * wrappers accept HEARTBEAT and COMPOUND respectively. No command decoding.
 */
int js8_compound_fields_decode(const uint8_t bits[JS8_PAYLOAD_BITS], Js8CompoundFields *out);
int js8_beacon_decode(const uint8_t bits[JS8_PAYLOAD_BITS], Js8BeaconFrame *out);
int js8_compound_identity_decode(const uint8_t bits[JS8_PAYLOAD_BITS], Js8CompoundIdentity *out);

/* is_cq must be 0/1 and subtype 0..7; otherwise returns "INVALID". */
const char *js8_beacon_name(int is_cq, uint8_t subtype);

#endif

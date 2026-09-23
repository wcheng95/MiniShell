#ifndef JS8_PROTOCOL_FRAME_H
#define JS8_PROTOCOL_FRAME_H

#include "js8_crc.h"

/* Normal application-prefix classes; values follow v3.0.3 FrameType.
 * These are independent of the transmission flags in payload bits 72..74.
 */
typedef enum {
    JS8_APP_FRAME_HEARTBEAT = 0,
    JS8_APP_FRAME_COMPOUND = 1,
    JS8_APP_FRAME_COMPOUND_DIRECTED = 2,
    JS8_APP_FRAME_DIRECTED = 3,
    JS8_APP_FRAME_DATA = 4,
    JS8_APP_FRAME_DATA_COMPRESSED = 6
} Js8AppFrameClass;

enum {
    JS8_TX_FLAG_FIRST = 1u,
    JS8_TX_FLAG_LAST = 2u,
    JS8_TX_FLAG_DATA = 4u
};

typedef struct {
    Js8AppFrameClass app_class;
    uint8_t raw_app_prefix3;
    uint8_t tx_flags;
    int first;
    int last;
    int data_flag;
} Js8ProtocolEnvelope;

/* Unpacked 0/1 bits. Both pointers required; input/output must not overlap.
 * Returns 0 on success, -1 on invalid input with output unchanged.
 * Classifies the Normal application prefix even when DATA is set; that flag
 * is reported only, without interpreting upstream's alternate data format.
 * Does not validate CRC or the contents of the application frame.
 */
int js8_protocol_envelope_decode(const uint8_t payload_bits[JS8_PAYLOAD_BITS],
                                 Js8ProtocolEnvelope *out);

/* Immutable diagnostic names; invalid values return "unknown" / "INVALID". */
const char *js8_app_frame_class_name(Js8AppFrameClass app_class);
const char *js8_tx_flags_name(uint8_t tx_flags);

#endif

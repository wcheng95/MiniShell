#ifndef JS8_FRAME_H
#define JS8_FRAME_H

#include "js8_crc.h"

typedef struct {
    char text12[13];
    uint8_t type;
} Js8PhysicalFrame;

/* Physical 12 x 6-bit alphabet words plus 3-bit type, not application text.
 * Input is 75 unpacked 0/1 bits. Both pointers are required and storage must
 * not overlap. Returns 0 on success, -1 on invalid input with output unchanged.
 * LDPC/CRC validation belongs to the caller before presenting received frames.
 */
int js8_frame_unpack(const uint8_t payload_bits[JS8_PAYLOAD_BITS],
                     Js8PhysicalFrame *frame);

#endif

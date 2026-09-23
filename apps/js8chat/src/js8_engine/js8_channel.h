#ifndef JS8_CHANNEL_H
#define JS8_CHANNEL_H

#include "js8_crc.h"

#define JS8_TONE_COUNT 79u
#define JS8_DATA_TONE_COUNT 58u
#define JS8_SYMBOL_PERIOD_MS 160u
#define JS8_TONE_SPACING_HZ 6.25f

/* JS8 Normal only. Input is exactly 75 unpacked 0/1 bits, MSB-first.
 * Caller supplies fixed-size, non-overlapping input/output arrays.
 * Returns 0 with 79 tones in 0..7, or -1 for NULL pointers/invalid bits,
 * leaving output unchanged. Data tones are direct binary, not Gray mapped.
 * This function owns no clock, waveform or transmission state.
 */
int js8_channel_encode(const uint8_t payload_bits[JS8_PAYLOAD_BITS],
                       uint8_t tones[JS8_TONE_COUNT]);

#endif

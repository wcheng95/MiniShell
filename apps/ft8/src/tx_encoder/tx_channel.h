#ifndef FT8_TX_CHANNEL_H
#define FT8_TX_CHANNEL_H
#include <stdint.h>

/* Private FT8-only 77-bit payload -> CRC/LDPC -> 79 tones. */
void ft8_tx_channel_encode(const uint8_t payload[10], uint8_t tones[79]);
#endif

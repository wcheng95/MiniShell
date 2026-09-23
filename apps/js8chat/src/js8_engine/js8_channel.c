#include "js8_channel.h"
#include "js8_ldpc.h"

#include <string.h>

int js8_channel_encode(const uint8_t payload_bits[JS8_PAYLOAD_BITS],
                       uint8_t tones[JS8_TONE_COUNT])
{
    static const uint8_t costas[7] = {4, 2, 5, 6, 1, 3, 0};
    uint8_t info[JS8_INFO_BITS];
    uint8_t codeword[JS8_CODEWORD_BITS];

    if (!tones || js8_crc12_append(payload_bits, info) != 0)
        return -1;
    if (js8_ldpc_encode(info, codeword) != 0)
        return -1;

    /* JS8::encode() v3.0.3 places parity before information, with the same
     * ORIGINAL Costas sequence at all three Normal sync positions.
     */
    for (unsigned half = 0; half < 2; ++half) {
        unsigned offset = half * 36u;
        memcpy(tones + offset, costas, sizeof(costas));
        for (unsigned word = 0; word < 29; ++word) {
            unsigned bit = half * JS8_INFO_BITS + word * 3u;
            tones[offset + 7u + word] = (uint8_t)((codeword[bit] << 2) |
                (codeword[bit + 1] << 1) | codeword[bit + 2]);
        }
    }
    memcpy(tones + 72, costas, sizeof(costas));
    return 0;
}

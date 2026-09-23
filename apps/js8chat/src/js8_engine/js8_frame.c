#include "js8_frame.h"

int js8_frame_unpack(const uint8_t payload_bits[JS8_PAYLOAD_BITS],
                     Js8PhysicalFrame *frame)
{
    static const char alphabet[] =
        "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz-+";
    if (!payload_bits || !frame)
        return -1;
    for (unsigned i = 0; i < JS8_PAYLOAD_BITS; ++i)
        if (payload_bits[i] > 1)
            return -1;
    for (unsigned i = 0; i < 12; ++i) {
        unsigned word = 0;
        for (unsigned j = 0; j < 6; ++j)
            word = (word << 1) | payload_bits[i * 6 + j];
        frame->text12[i] = alphabet[word];
    }
    frame->text12[12] = '\0';
    frame->type = (uint8_t)((payload_bits[72] << 2) |
                           (payload_bits[73] << 1) | payload_bits[74]);
    return 0;
}

#include "js8_protocol_frame.h"

int js8_protocol_envelope_decode(const uint8_t payload_bits[JS8_PAYLOAD_BITS],
                                 Js8ProtocolEnvelope *out)
{
    if (!payload_bits || !out)
        return -1;
    for (unsigned i = 0; i < JS8_PAYLOAD_BITS; ++i)
        if (payload_bits[i] > 1)
            return -1;
    unsigned prefix = (payload_bits[0] << 2) | (payload_bits[1] << 1) | payload_bits[2];
    /* For 10X and 11X the third bit belongs to the data, not the class. */
    out->app_class = (Js8AppFrameClass)(prefix < 4 ? prefix : prefix & 6u);
    out->raw_app_prefix3 = (uint8_t)prefix;
    out->tx_flags = (uint8_t)((payload_bits[72] << 2) |
                              (payload_bits[73] << 1) | payload_bits[74]);
    out->first = (out->tx_flags & JS8_TX_FLAG_FIRST) != 0;
    out->last = (out->tx_flags & JS8_TX_FLAG_LAST) != 0;
    out->data_flag = (out->tx_flags & JS8_TX_FLAG_DATA) != 0;
    return 0;
}

const char *js8_app_frame_class_name(Js8AppFrameClass app_class)
{
    switch (app_class) {
    case JS8_APP_FRAME_HEARTBEAT: return "heartbeat";
    case JS8_APP_FRAME_COMPOUND: return "compound";
    case JS8_APP_FRAME_COMPOUND_DIRECTED: return "compound_directed";
    case JS8_APP_FRAME_DIRECTED: return "directed";
    case JS8_APP_FRAME_DATA: return "data";
    case JS8_APP_FRAME_DATA_COMPRESSED: return "data_compressed";
    default: return "unknown";
    }
}

const char *js8_tx_flags_name(uint8_t tx_flags)
{
    static const char *const names[8] = {
        "NONE", "FIRST", "LAST", "FIRST|LAST", "DATA", "FIRST|DATA",
        "LAST|DATA", "FIRST|LAST|DATA"
    };
    return tx_flags < 8 ? names[tx_flags] : "INVALID";
}

#include "js8_compound.h"

#include <string.h>

int js8_callsign50_unpack(uint64_t packed, char out[12])
{
    static const char alphabet[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ /@";
    char word[11];
    if (!out || packed >= (UINT64_C(1) << 50))
        return -1;
    for (int i = 10; i >= 0; --i) {
        unsigned radix = (i == 3 || i == 7) ? 2 : (i == 0 ? 39 : 38);
        unsigned digit = (unsigned)(packed % radix);
        packed /= radix;
        word[i] = radix == 2 ? (digit ? '/' : ' ') : alphabet[digit];
    }
    unsigned n = 0;
    for (unsigned i = 0; i < 11; ++i)
        if (word[i] != ' ')
            out[n++] = word[i];
    out[n] = '\0';
    return 0;
}

int js8_grid_unpack(uint16_t packed, char out[5])
{
    if (!out)
        return -1;
    if (packed > 32400) {
        out[0] = '\0';
        return 0;
    }
    /* Integer reduction of unpackGrid -> deg2grid(). At 32400 upstream
     * wraps longitude 182 to -178, yielding RA90 (not a no-grid sentinel).
     */
    unsigned longitude = packed == 32400 ? 179 : 179 - packed / 180;
    unsigned latitude = packed % 180;
    out[0] = (char)('A' + longitude / 10);
    out[1] = (char)('A' + latitude / 10);
    out[2] = (char)('0' + longitude % 10);
    out[3] = (char)('0' + latitude % 10);
    out[4] = '\0';
    return 0;
}

static uint64_t read_bits(const uint8_t *bits, unsigned count)
{
    uint64_t value = 0;
    for (unsigned i = 0; i < count; ++i)
        value = (value << 1) | bits[i];
    return value;
}

int js8_compound_fields_decode(const uint8_t bits[JS8_PAYLOAD_BITS], Js8CompoundFields *out)
{
    Js8ProtocolEnvelope envelope;
    if (!out || js8_protocol_envelope_decode(bits, &envelope) ||
        (envelope.app_class != JS8_APP_FRAME_HEARTBEAT &&
         envelope.app_class != JS8_APP_FRAME_COMPOUND &&
         envelope.app_class != JS8_APP_FRAME_COMPOUND_DIRECTED))
        return -1;
    Js8CompoundFields result = {0};
    result.app_class = envelope.app_class;
    js8_callsign50_unpack(read_bits(bits + 3, 50), result.callsign);
    result.extra16 = (uint16_t)read_bits(bits + 53, 16);
    result.bits3 = (uint8_t)read_bits(bits + 69, 3);
    *out = result;
    return 0;
}

int js8_beacon_decode(const uint8_t bits[JS8_PAYLOAD_BITS], Js8BeaconFrame *out)
{
    Js8CompoundFields fields;
    if (!out || js8_compound_fields_decode(bits, &fields) ||
        fields.app_class != JS8_APP_FRAME_HEARTBEAT)
        return -1;
    Js8BeaconFrame result = {0};
    memcpy(result.callsign, fields.callsign, sizeof(result.callsign));
    js8_grid_unpack(fields.extra16 & 32767u, result.grid);
    result.has_grid = result.grid[0] != '\0';
    result.is_cq = (fields.extra16 & 32768u) != 0;
    result.subtype = fields.bits3;
    *out = result;
    return 0;
}

int js8_compound_identity_decode(const uint8_t bits[JS8_PAYLOAD_BITS], Js8CompoundIdentity *out)
{
    Js8CompoundFields fields;
    if (!out || js8_compound_fields_decode(bits, &fields) ||
        fields.app_class != JS8_APP_FRAME_COMPOUND)
        return -1;
    Js8CompoundIdentity result = {0};
    memcpy(result.callsign, fields.callsign, sizeof(result.callsign));
    js8_grid_unpack(fields.extra16, result.grid);
    result.has_grid = result.grid[0] != '\0';
    result.extra16 = fields.extra16;
    result.bits3 = fields.bits3;
    *out = result;
    return 0;
}

const char *js8_beacon_name(int is_cq, uint8_t subtype)
{
    static const char *const cqs[] = {
        "CQ CQ CQ", "CQ DX", "CQ QRP", "CQ CONTEST", "CQ FIELD", "CQ FD", "CQ CQ", "CQ"
    };
    if ((is_cq != 0 && is_cq != 1) || subtype > 7)
        return "INVALID";
    return is_cq ? cqs[subtype] : "HB";
}

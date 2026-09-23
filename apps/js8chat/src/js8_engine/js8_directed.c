#include "js8_directed.h"

#include <string.h>

/* Varicode.cpp v3.0.3 basecalls, in numeric order nbasecall+1 .. +54. */
static const char *const basecalls[] = {
    "<....>", "@ALLCALL", "@JS8NET", "@DX/NA", "@DX/SA", "@DX/EU",
    "@DX/AS", "@DX/AF", "@DX/OC", "@DX/AN", "@REGION/1", "@REGION/2",
    "@REGION/3", "@GROUP/0", "@GROUP/1", "@GROUP/2", "@GROUP/3", "@GROUP/4",
    "@GROUP/5", "@GROUP/6", "@GROUP/7", "@GROUP/8", "@GROUP/9", "@COMMAND",
    "@CONTROL", "@NET", "@NTS", "@RESERVE/0", "@RESERVE/1", "@RESERVE/2",
    "@RESERVE/3", "@RESERVE/4", "@APRSIS", "@RAGCHEW", "@JS8", "@EMCOMM",
    "@ARES", "@MARS", "@AMRRON", "@RACES", "@RAYNET", "@RADAR", "@SKYWARN",
    "@CQ", "@HB", "@QSO", "@QSOPARTY", "@CONTEST", "@FIELDDAY", "@SOTA",
    "@IOTA", "@POTA", "@QRP", "@QRO"
};

int js8_callsign28_unpack(uint32_t packed, int portable, char out[JS8_CALLSIGN28_SIZE])
{
    static const char alphabet[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ /@";
    const uint32_t nbasecall = 262177560;
    if (!out || packed >= (UINT32_C(1) << 28))
        return -1;
    if (packed > nbasecall && packed <= nbasecall + 54) {
        strcpy(out, basecalls[packed - nbasecall - 1]);
        return 0; /* Upstream specials ignore the portable flag. */
    }
    char word[7];
    word[6] = '\0';
    for (int i = 5; i >= 3; --i) {
        word[i] = alphabet[packed % 27 + 10];
        packed /= 27;
    }
    word[2] = alphabet[packed % 10];
    packed /= 10;
    word[1] = alphabet[packed % 36];
    packed /= 36;
    word[0] = alphabet[packed]; /* At most 37 for every 28-bit input. */
    char expanded[8];
    if (!strncmp(word, "3D0", 3)) {
        memcpy(expanded, "3DA0", 4);
        strcpy(expanded + 4, word + 3);
    } else if (word[0] == 'Q' && word[1] >= 'A' && word[1] <= 'Z') {
        memcpy(expanded, "3X", 2);
        strcpy(expanded + 2, word + 1);
    } else {
        strcpy(expanded, word);
    }
    char *start = expanded;
    while (*start == ' ') ++start;
    size_t length = strlen(start);
    while (length && start[length - 1] == ' ') --length;
    memcpy(out, start, length);
    if (portable) {
        out[length++] = '/';
        out[length++] = 'P';
    }
    out[length] = '\0';
    return 0;
}

const char *js8_directed_command_name(uint8_t code)
{
    static const char *const names[] = {
        " SNR?", " DIT DIT", " NACK", " HEARING?", " GRID?", ">", " STATUS?", " STATUS",
        " HEARING", " MSG", " MSG TO:", " QUERY", " QUERY MSGS", " QUERY CALL", " ACK", " GRID",
        " INFO?", " INFO", " FB", " HW CPY?", " SK", " RR", " QSL?", " QSL",
        " CMD", " SNR", " NO", " YES", " 73", " HEARTBEAT SNR", " AGN?", " "
    };
    return code < 32 ? names[code] : "INVALID";
}

int js8_directed_format_snr(int number, char out[4])
{
    if (!out) return -1;
    if (number < -60 || number > 60) {
        out[0] = '\0';
        return 0;
    }
    out[0] = number < 0 ? '-' : '+';
    unsigned magnitude = (unsigned)(number < 0 ? -number : number);
    out[1] = (char)('0' + magnitude / 10);
    out[2] = (char)('0' + magnitude % 10);
    out[3] = '\0';
    return 0;
}

static uint32_t read_bits(const uint8_t *bits, unsigned count)
{
    uint32_t value = 0;
    for (unsigned i = 0; i < count; ++i) value = (value << 1) | bits[i];
    return value;
}

int js8_directed_decode(const uint8_t bits[JS8_PAYLOAD_BITS], Js8DirectedFrame *out)
{
    Js8ProtocolEnvelope envelope;
    if (!out || js8_protocol_envelope_decode(bits, &envelope) ||
        envelope.app_class != JS8_APP_FRAME_DIRECTED)
        return -1;
    Js8DirectedFrame result = {0};
    result.from_packed = read_bits(bits + 3, 28);
    result.to_packed = read_bits(bits + 31, 28);
    result.command_code = (uint8_t)read_bits(bits + 59, 5);
    result.portable_from = bits[64];
    result.portable_to = bits[65];
    js8_callsign28_unpack(result.from_packed, result.portable_from, result.from);
    js8_callsign28_unpack(result.to_packed, result.portable_to, result.to);
    unsigned number = read_bits(bits + 66, 6);
    result.has_number = number != 0;
    result.number = number ? (int)number - 31 : 0;
    result.is_free_text = result.command_code == 31;
    result.is_ack = result.command_code == 14;
    result.is_73 = result.command_code == 28;
    result.is_snr = result.command_code == 25 || result.command_code == 29;
    *out = result;
    return 0;
}

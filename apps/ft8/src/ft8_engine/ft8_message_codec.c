#include "ft8_message_codec.h"

#include <stdio.h>
#include <string.h>

#define FT8_MAX22 UINT32_C(4194304)
#define FT8_NTOKENS UINT32_C(2063592)
#define FT8_MAXGRID4 UINT16_C(32400)

#define FT8_ARRL_SECTIONS_COUNT 84u

static const char *const k_arrl_sections[FT8_ARRL_SECTIONS_COUNT] = {
    "AB",  "AK",  "AL",  "AR",  "AZ",  "BC",  "CO",  "CT",  "DE",  "EB",
    "EMA", "ENY", "EPA", "EWA", "GA",  "GTA", "IA",  "ID",  "IL",  "IN",
    "KS",  "KY",  "LA",  "LAX", "MAR", "MB",  "MDC", "ME",  "MI",  "MN",
    "MO",  "MS",  "MT",  "NC",  "ND",  "NE",  "NFL", "NH",  "NL",  "NLI",
    "NM",  "NNJ", "NNY", "NT",  "NTX", "NV",  "OH",  "OK",  "ONE", "ONN",
    "ONS", "OR",  "ORG", "PAC", "PR",  "QC",  "RI",  "SB",  "SC",  "SCV",
    "SD",  "SDG", "SF",  "SFL", "SJV", "SK",  "SNJ", "STX", "SV",  "TN",
    "UT",  "VA",  "VI",  "VT",  "WCF", "WI",  "WMA", "WNY", "WPA", "WTX",
    "WV",  "WWA", "WY",  "DX"
};

typedef enum {
    CHAR_TABLE_FULL = 0,
    CHAR_TABLE_ALPHANUM_SPACE_SLASH,
    CHAR_TABLE_ALPHANUM_SPACE,
    CHAR_TABLE_ALPHANUM,
    CHAR_TABLE_NUMERIC,
    CHAR_TABLE_LETTERS_SPACE
} CharTable;

static char charn_local(unsigned value, CharTable table)
{
    int c = (int)value;

    if (table != CHAR_TABLE_ALPHANUM && table != CHAR_TABLE_NUMERIC) {
        if (c == 0)
            return ' ';
        --c;
    }
    if (table != CHAR_TABLE_LETTERS_SPACE) {
        if (c < 10)
            return (char)('0' + c);
        c -= 10;
    }
    if (table != CHAR_TABLE_NUMERIC) {
        if (c < 26)
            return (char)('A' + c);
        c -= 26;
    }
    if (table == CHAR_TABLE_FULL) {
        static const char tail[] = "+-./?";
        if (c >= 0 && c < 5)
            return tail[c];
    } else if (table == CHAR_TABLE_ALPHANUM_SPACE_SLASH && c == 0) {
        return '/';
    }

    return '_';
}

static int nchar_local(char c, CharTable table)
{
    int n = 0;

    if (table != CHAR_TABLE_ALPHANUM && table != CHAR_TABLE_NUMERIC) {
        if (c == ' ')
            return n;
        ++n;
    }
    if (table != CHAR_TABLE_LETTERS_SPACE) {
        if (c >= '0' && c <= '9')
            return n + (c - '0');
        n += 10;
    }
    if (table != CHAR_TABLE_NUMERIC) {
        if (c >= 'A' && c <= 'Z')
            return n + (c - 'A');
        n += 26;
    }
    if (table == CHAR_TABLE_FULL) {
        const char *p = strchr("+-./?", c);
        if (p != NULL)
            return n + (int)(p - "+-./?");
    } else if (table == CHAR_TABLE_ALPHANUM_SPACE_SLASH && c == '/') {
        return n;
    }

    return -1;
}

static void copy_text(char *dst, size_t capacity, const char *src)
{
    if (dst == NULL || capacity == 0)
        return;
    if (src == NULL)
        src = "";
    (void)snprintf(dst, capacity, "%s", src);
}

static void trim_copy(char *dst, size_t capacity, const char *src)
{
    size_t begin = 0;
    size_t end;
    size_t length;

    if (dst == NULL || capacity == 0)
        return;
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }

    end = strlen(src);
    while (src[begin] == ' ')
        ++begin;
    while (end > begin && src[end - 1] == ' ')
        --end;

    length = end - begin;
    if (length >= capacity)
        length = capacity - 1;
    memcpy(dst, src + begin, length);
    dst[length] = '\0';
}

static bool text_append(char *dst, size_t capacity, const char *text)
{
    size_t used;
    size_t needed;

    if (dst == NULL || text == NULL || capacity == 0)
        return false;
    used = strlen(dst);
    needed = strlen(text);
    if (used + needed >= capacity)
        return false;
    memcpy(dst + used, text, needed + 1);
    return true;
}

static bool text_append_field(char *dst, size_t capacity, const char *field)
{
    if (field == NULL || field[0] == '\0')
        return true;
    if (dst[0] != '\0' && !text_append(dst, capacity, " "))
        return false;
    return text_append(dst, capacity, field);
}

static uint32_t get_bits_be(const uint8_t *bytes, unsigned bitpos, unsigned nbits)
{
    uint32_t value = 0;
    unsigned i;

    for (i = 0; i < nbits; ++i) {
        unsigned p = bitpos + i;
        uint8_t byte = bytes[p >> 3];
        unsigned shift = 7u - (p & 7u);
        value = (value << 1) | ((byte >> shift) & 1u);
    }
    return value;
}

static void format_signed_dd(char out[4], int value)
{
    char sign = '+';
    unsigned magnitude;

    if (value < 0) {
        sign = '-';
        magnitude = (unsigned)(-value);
    } else {
        magnitude = (unsigned)value;
    }
    out[0] = sign;
    out[1] = (char)('0' + ((magnitude / 10u) % 10u));
    out[2] = (char)('0' + (magnitude % 10u));
    out[3] = '\0';
}

uint8_t ft8_protocol_get_i3(const uint8_t payload[FT8_PAYLOAD_BYTES])
{
    if (payload == NULL)
        return 0xFFu;
    return (uint8_t)((payload[9] >> 3) & 0x07u);
}

uint8_t ft8_protocol_get_n3(const uint8_t payload[FT8_PAYLOAD_BYTES])
{
    if (payload == NULL)
        return 0xFFu;
    return (uint8_t)(((payload[8] << 2) & 0x04u) | ((payload[9] >> 6) & 0x03u));
}

Ft8ProtocolType ft8_protocol_get_type(const uint8_t payload[FT8_PAYLOAD_BYTES])
{
    uint8_t i3;
    uint8_t n3;

    if (payload == NULL)
        return FT8_PROTOCOL_UNKNOWN;

    i3 = ft8_protocol_get_i3(payload);
    switch (i3) {
    case 0:
        n3 = ft8_protocol_get_n3(payload);
        switch (n3) {
        case 0: return FT8_PROTOCOL_FREE_TEXT;
        case 1: return FT8_PROTOCOL_DXPEDITION;
        case 2: return FT8_PROTOCOL_EU_VHF;
        case 3:
        case 4: return FT8_PROTOCOL_ARRL_FD;
        case 5: return FT8_PROTOCOL_TELEMETRY;
        default: return FT8_PROTOCOL_UNKNOWN;
        }
    case 1:
    case 2:
        return FT8_PROTOCOL_STANDARD;
    case 3:
        return FT8_PROTOCOL_ARRL_RTTY;
    case 4:
        return FT8_PROTOCOL_NONSTD_CALL;
    case 5:
        return FT8_PROTOCOL_WWROF;
    default:
        return FT8_PROTOCOL_UNKNOWN;
    }
}

Ft8ProtocolCodecStatus ft8_protocol_callsign_hash22(const char *callsign,
                                                    uint32_t *out_hash22)
{
    uint64_t n58 = 0;
    size_t i = 0;

    if (callsign == NULL || callsign[0] == '\0' || out_hash22 == NULL)
        return FT8_PROTOCOL_CODEC_ERR_INVALID;

    while (callsign[i] != '\0' && i < FT8_HASH_STORE_CALLSIGN_MAX) {
        int j = nchar_local(callsign[i], CHAR_TABLE_ALPHANUM_SPACE_SLASH);
        if (j < 0)
            return FT8_PROTOCOL_CODEC_ERR_INVALID;
        n58 = (UINT64_C(38) * n58) + (uint64_t)j;
        ++i;
    }
    if (callsign[i] != '\0')
        return FT8_PROTOCOL_CODEC_ERR_INVALID;

    while (i < FT8_HASH_STORE_CALLSIGN_MAX) {
        n58 *= UINT64_C(38);
        ++i;
    }

    *out_hash22 = (uint32_t)((UINT64_C(47055833459) * n58) >> (64 - 22)) & UINT32_C(0x003FFFFF);
    return FT8_PROTOCOL_CODEC_OK;
}

static void save_decoded_callsign(Ft8HashStore *store, const char *callsign)
{
    uint32_t hash22;

    if (store == NULL || callsign == NULL || callsign[0] == '\0')
        return;
    if (ft8_protocol_callsign_hash22(callsign, &hash22) != FT8_PROTOCOL_CODEC_OK)
        return;
    (void)ft8_hash_store_save(store, callsign, hash22);
}

static void resolve_hash(Ft8HashStore *store,
                         Ft8HashType type,
                         uint32_t hash,
                         char out[FT8_PROTOCOL_CALL_CAP],
                         bool *has_unresolved_hash)
{
    char plain[FT8_HASH_STORE_CALLSIGN_CAP];
    Ft8HashStoreStatus status = FT8_HASH_STORE_NOT_FOUND;

    if (store != NULL)
        status = ft8_hash_store_lookup(store, type, hash, plain, sizeof(plain));

    if (status == FT8_HASH_STORE_OK) {
        (void)snprintf(out, FT8_PROTOCOL_CALL_CAP, "<%s>", plain);
    } else {
        copy_text(out, FT8_PROTOCOL_CALL_CAP, "<...>");
        if (has_unresolved_hash != NULL)
            *has_unresolved_hash = true;
    }
}

static int unpack28(uint32_t n28,
                    uint8_t ip,
                    uint8_t i3,
                    Ft8HashStore *store,
                    char out[FT8_PROTOCOL_CALL_CAP],
                    Ft8ProtocolFieldKind *kind,
                    bool *has_unresolved_hash)
{
    if (out == NULL || kind == NULL)
        return -1;

    if (n28 < FT8_NTOKENS) {
        if (n28 <= 2u) {
            copy_text(out, FT8_PROTOCOL_CALL_CAP,
                      n28 == 0u ? "DE" : (n28 == 1u ? "QRZ" : "CQ"));
            *kind = FT8_PROTOCOL_FIELD_TOKEN;
            return 0;
        }
        if (n28 <= 1002u) {
            unsigned modifier = (unsigned)(n28 - 3u);
            (void)snprintf(out, FT8_PROTOCOL_CALL_CAP, "CQ %03u", modifier);
            *kind = FT8_PROTOCOL_FIELD_TOKEN_WITH_ARG;
            return 0;
        }
        if (n28 <= UINT32_C(532443)) {
            uint32_t n = n28 - 1003u;
            char letters[5];
            char trimmed[5];
            int i;

            letters[4] = '\0';
            for (i = 3; i >= 0; --i) {
                letters[i] = charn_local(n % 27u, CHAR_TABLE_LETTERS_SPACE);
                if (i != 0)
                    n /= 27u;
            }
            trim_copy(trimmed, sizeof(trimmed), letters);
            (void)snprintf(out, FT8_PROTOCOL_CALL_CAP, "CQ %s", trimmed);
            *kind = FT8_PROTOCOL_FIELD_TOKEN_WITH_ARG;
            return 0;
        }
        return -1;
    }

    n28 -= FT8_NTOKENS;
    if (n28 < FT8_MAX22) {
        resolve_hash(store, FT8_HASH_22_BITS, n28, out, has_unresolved_hash);
        *kind = FT8_PROTOCOL_FIELD_CALL;
        return 0;
    }

    {
        uint32_t n = n28 - FT8_MAX22;
        char callsign[7];
        char base[FT8_PROTOCOL_CALL_CAP];

        callsign[6] = '\0';
        callsign[5] = charn_local(n % 27u, CHAR_TABLE_LETTERS_SPACE); n /= 27u;
        callsign[4] = charn_local(n % 27u, CHAR_TABLE_LETTERS_SPACE); n /= 27u;
        callsign[3] = charn_local(n % 27u, CHAR_TABLE_LETTERS_SPACE); n /= 27u;
        callsign[2] = charn_local(n % 10u, CHAR_TABLE_NUMERIC); n /= 10u;
        callsign[1] = charn_local(n % 36u, CHAR_TABLE_ALPHANUM); n /= 36u;
        callsign[0] = charn_local(n % 37u, CHAR_TABLE_ALPHANUM_SPACE);

        if (callsign[0] == '3' && callsign[1] == 'D' && callsign[2] == '0' && callsign[3] != ' ') {
            char tail[4];
            trim_copy(tail, sizeof(tail), callsign + 3);
            (void)snprintf(base, sizeof(base), "3DA0%s", tail);
        } else if (callsign[0] == 'Q' && callsign[1] >= 'A' && callsign[1] <= 'Z') {
            char tail[6];
            trim_copy(tail, sizeof(tail), callsign + 1);
            (void)snprintf(base, sizeof(base), "3X%s", tail);
        } else {
            trim_copy(base, sizeof(base), callsign);
        }

        if (strlen(base) < 3u)
            return -1;

        if (ip != 0u) {
            if (i3 == 1u) {
                if (strlen(base) + 2u >= sizeof(base))
                    return -1;
                strcat(base, "/R");
            } else if (i3 == 2u) {
                if (strlen(base) + 2u >= sizeof(base))
                    return -1;
                strcat(base, "/P");
            } else {
                return -2;
            }
        }

        copy_text(out, FT8_PROTOCOL_CALL_CAP, base);
        save_decoded_callsign(store, base);
        *kind = FT8_PROTOCOL_FIELD_CALL;
        return 0;
    }
}

static bool unpack58(uint64_t n58,
                     Ft8HashStore *store,
                     char out[FT8_PROTOCOL_CALL_CAP])
{
    char encoded[12];
    int i;

    encoded[11] = '\0';
    for (i = 10; i >= 0; --i) {
        encoded[i] = charn_local((unsigned)(n58 % UINT64_C(38)), CHAR_TABLE_ALPHANUM_SPACE_SLASH);
        if (i != 0)
            n58 /= UINT64_C(38);
    }

    trim_copy(out, FT8_PROTOCOL_CALL_CAP, encoded);
    if (strlen(out) >= 3u) {
        save_decoded_callsign(store, out);
        return true;
    }
    return false;
}

static int unpack_grid(uint16_t igrid4,
                       uint8_t ir,
                       char out[FT8_PROTOCOL_EXTRA_CAP],
                       Ft8ProtocolFieldKind *kind)
{
    if (out == NULL || kind == NULL)
        return -1;

    if (igrid4 <= FT8_MAXGRID4) {
        uint16_t n = igrid4;
        char grid[5];

        grid[4] = '\0';
        grid[3] = (char)('0' + (n % 10u)); n /= 10u;
        grid[2] = (char)('0' + (n % 10u)); n /= 10u;
        grid[1] = (char)('A' + (n % 18u)); n /= 18u;
        grid[0] = (char)('A' + (n % 18u));

        if (ir != 0u)
            (void)snprintf(out, FT8_PROTOCOL_EXTRA_CAP, "R %s", grid);
        else
            copy_text(out, FT8_PROTOCOL_EXTRA_CAP, grid);
        *kind = FT8_PROTOCOL_FIELD_GRID;
        return 0;
    }

    {
        int irpt = (int)igrid4 - (int)FT8_MAXGRID4;
        char report[4];

        switch (irpt) {
        case 1:
            out[0] = '\0';
            *kind = FT8_PROTOCOL_FIELD_NONE;
            return 0;
        case 2:
            copy_text(out, FT8_PROTOCOL_EXTRA_CAP, "RRR");
            *kind = FT8_PROTOCOL_FIELD_TOKEN;
            return 0;
        case 3:
            copy_text(out, FT8_PROTOCOL_EXTRA_CAP, "RR73");
            *kind = FT8_PROTOCOL_FIELD_TOKEN;
            return 0;
        case 4:
            copy_text(out, FT8_PROTOCOL_EXTRA_CAP, "73");
            *kind = FT8_PROTOCOL_FIELD_TOKEN;
            return 0;
        default:
            format_signed_dd(report, irpt - 35);
            if (ir != 0u)
                (void)snprintf(out, FT8_PROTOCOL_EXTRA_CAP, "R%s", report);
            else
                copy_text(out, FT8_PROTOCOL_EXTRA_CAP, report);
            *kind = FT8_PROTOCOL_FIELD_REPORT;
            return 0;
        }
    }
}

static void decode_telemetry_bytes(const uint8_t payload[FT8_PAYLOAD_BYTES],
                                   uint8_t bytes[FT8_PROTOCOL_TELEMETRY_BYTES])
{
    uint8_t carry = 0;
    size_t i;

    for (i = 0; i < FT8_PROTOCOL_TELEMETRY_BYTES; ++i) {
        bytes[i] = (uint8_t)((carry << 7) | (payload[i] >> 1));
        carry = (uint8_t)(payload[i] & 0x01u);
    }
}

static Ft8ProtocolCodecStatus decode_standard(const uint8_t payload[FT8_PAYLOAD_BYTES],
                                              Ft8HashStore *store,
                                              Ft8ProtocolMessage *out)
{
    uint32_t n29a;
    uint32_t n29b;
    uint16_t igrid4;
    uint8_t ir;
    uint8_t i3 = ft8_protocol_get_i3(payload);
    Ft8ProtocolStandard *data = &out->data.standard;

    n29a = ((uint32_t)payload[0] << 21);
    n29a |= ((uint32_t)payload[1] << 13);
    n29a |= ((uint32_t)payload[2] << 5);
    n29a |= ((uint32_t)payload[3] >> 3);
    n29b = ((uint32_t)(payload[3] & 0x07u) << 26);
    n29b |= ((uint32_t)payload[4] << 18);
    n29b |= ((uint32_t)payload[5] << 10);
    n29b |= ((uint32_t)payload[6] << 2);
    n29b |= ((uint32_t)payload[7] >> 6);
    ir = (uint8_t)((payload[7] & 0x20u) >> 5);
    igrid4 = (uint16_t)(((uint16_t)(payload[7] & 0x1Fu) << 10) |
                        ((uint16_t)payload[8] << 2) |
                        ((uint16_t)payload[9] >> 6));

    if (unpack28(n29a >> 1, (uint8_t)(n29a & 1u), i3, store,
                 data->call_to, &data->call_to_kind, &out->has_unresolved_hash) < 0)
        return FT8_PROTOCOL_CODEC_MALFORMED;
    if (unpack28(n29b >> 1, (uint8_t)(n29b & 1u), i3, store,
                 data->call_de, &(Ft8ProtocolFieldKind){FT8_PROTOCOL_FIELD_CALL},
                 &out->has_unresolved_hash) < 0)
        return FT8_PROTOCOL_CODEC_MALFORMED;
    if (unpack_grid(igrid4, ir, data->extra, &data->extra_kind) < 0)
        return FT8_PROTOCOL_CODEC_MALFORMED;

    if (!text_append_field(out->canonical_text, sizeof(out->canonical_text), data->call_to) ||
        !text_append_field(out->canonical_text, sizeof(out->canonical_text), data->call_de) ||
        !text_append_field(out->canonical_text, sizeof(out->canonical_text), data->extra))
        return FT8_PROTOCOL_CODEC_MALFORMED;

    return FT8_PROTOCOL_CODEC_OK;
}

static Ft8ProtocolCodecStatus decode_nonstandard(const uint8_t payload[FT8_PAYLOAD_BYTES],
                                                 Ft8HashStore *store,
                                                 Ft8ProtocolMessage *out)
{
    uint16_t n12;
    uint16_t iflip;
    uint16_t nrpt;
    uint16_t icq;
    uint64_t n58;
    char decoded[FT8_PROTOCOL_CALL_CAP];
    char hashed[FT8_PROTOCOL_CALL_CAP];
    const char *call1;
    const char *call2;
    Ft8ProtocolNonstandard *data = &out->data.nonstandard;

    n12 = (uint16_t)(((uint16_t)payload[0] << 4) | (payload[1] >> 4));
    n58 = ((uint64_t)(payload[1] & 0x0Fu) << 54) |
          ((uint64_t)payload[2] << 46) |
          ((uint64_t)payload[3] << 38) |
          ((uint64_t)payload[4] << 30) |
          ((uint64_t)payload[5] << 22) |
          ((uint64_t)payload[6] << 14) |
          ((uint64_t)payload[7] << 6) |
          ((uint64_t)payload[8] >> 2);
    iflip = (uint16_t)((payload[8] >> 1) & 0x01u);
    nrpt = (uint16_t)(((payload[8] & 0x01u) << 1) | (payload[9] >> 7));
    icq = (uint16_t)((payload[9] >> 6) & 0x01u);

    (void)unpack58(n58, store, decoded);
    resolve_hash(store, FT8_HASH_12_BITS, n12, hashed, &out->has_unresolved_hash);

    call1 = iflip != 0u ? decoded : hashed;
    call2 = iflip != 0u ? hashed : decoded;
    data->is_cq = icq != 0u;

    if (data->is_cq) {
        copy_text(data->call_to, sizeof(data->call_to), "CQ");
        data->terminal = FT8_PROTOCOL_TERMINAL_NONE;
    } else {
        copy_text(data->call_to, sizeof(data->call_to), call1);
        data->terminal = nrpt == 1u ? FT8_PROTOCOL_TERMINAL_RRR :
                         nrpt == 2u ? FT8_PROTOCOL_TERMINAL_RR73 :
                         nrpt == 3u ? FT8_PROTOCOL_TERMINAL_73 :
                                      FT8_PROTOCOL_TERMINAL_NONE;
    }
    copy_text(data->call_de, sizeof(data->call_de), call2);

    if (!text_append_field(out->canonical_text, sizeof(out->canonical_text), data->call_to) ||
        !text_append_field(out->canonical_text, sizeof(out->canonical_text), data->call_de))
        return FT8_PROTOCOL_CODEC_MALFORMED;

    if (!data->is_cq) {
        const char *terminal = data->terminal == FT8_PROTOCOL_TERMINAL_RRR ? "RRR" :
                               data->terminal == FT8_PROTOCOL_TERMINAL_RR73 ? "RR73" :
                               data->terminal == FT8_PROTOCOL_TERMINAL_73 ? "73" : "";
        if (!text_append_field(out->canonical_text, sizeof(out->canonical_text), terminal))
            return FT8_PROTOCOL_CODEC_MALFORMED;
    }

    return FT8_PROTOCOL_CODEC_OK;
}

static Ft8ProtocolCodecStatus decode_arrl_fd(const uint8_t payload[FT8_PAYLOAD_BYTES],
                                             Ft8HashStore *store,
                                             Ft8ProtocolMessage *out)
{
    uint8_t i3 = (uint8_t)get_bits_be(payload, 74, 3);
    uint8_t n3 = (uint8_t)get_bits_be(payload, 71, 3);
    uint32_t c28a;
    uint32_t c28b;
    uint8_t r1;
    uint8_t n4;
    uint8_t k3;
    uint8_t s7;
    Ft8ProtocolFieldKind ignored_kind;
    Ft8ProtocolArrlFd *data = &out->data.arrl_fd;

    if (i3 != 0u || (n3 != 3u && n3 != 4u))
        return FT8_PROTOCOL_CODEC_MALFORMED;

    c28a = get_bits_be(payload, 0, 28);
    c28b = get_bits_be(payload, 28, 28);
    r1 = (uint8_t)get_bits_be(payload, 56, 1);
    n4 = (uint8_t)get_bits_be(payload, 57, 4);
    k3 = (uint8_t)get_bits_be(payload, 61, 3);
    s7 = (uint8_t)get_bits_be(payload, 64, 7);

    if (unpack28(c28a, 0, i3, store, data->call_to, &ignored_kind, &out->has_unresolved_hash) < 0 ||
        unpack28(c28b, 0, i3, store, data->call_de, &ignored_kind, &out->has_unresolved_hash) < 0)
        return FT8_PROTOCOL_CODEC_MALFORMED;
    if (k3 > 5u || s7 >= FT8_ARRL_SECTIONS_COUNT)
        return FT8_PROTOCOL_CODEC_MALFORMED;

    data->has_r = r1 != 0u;
    data->transmitter_count = (uint8_t)(n3 == 3u ? n4 + 1u : n4 + 17u);
    data->class_letter = (char)('A' + k3);
    copy_text(data->section, sizeof(data->section), k_arrl_sections[s7]);

    if (data->has_r) {
        (void)snprintf(out->canonical_text, sizeof(out->canonical_text),
                       "%s %s R %u%c %s", data->call_to, data->call_de,
                       (unsigned)data->transmitter_count, data->class_letter, data->section);
    } else {
        (void)snprintf(out->canonical_text, sizeof(out->canonical_text),
                       "%s %s %u%c %s", data->call_to, data->call_de,
                       (unsigned)data->transmitter_count, data->class_letter, data->section);
    }

    return FT8_PROTOCOL_CODEC_OK;
}

static Ft8ProtocolCodecStatus decode_dxpedition(const uint8_t payload[FT8_PAYLOAD_BYTES],
                                                Ft8HashStore *store,
                                                Ft8ProtocolMessage *out)
{
    uint8_t i3 = (uint8_t)get_bits_be(payload, 74, 3);
    uint8_t n3 = (uint8_t)get_bits_be(payload, 71, 3);
    uint16_t h10;
    uint8_t r5;
    uint32_t c28a;
    uint32_t c28b;
    Ft8ProtocolFieldKind ignored_kind;
    char report[4];
    Ft8ProtocolDxpedition *data = &out->data.dxpedition;

    if (i3 != 0u || n3 != 1u)
        return FT8_PROTOCOL_CODEC_MALFORMED;

    h10 = (uint16_t)get_bits_be(payload, 0, 10);
    r5 = (uint8_t)get_bits_be(payload, 10, 5);
    c28a = get_bits_be(payload, 15, 28);
    c28b = get_bits_be(payload, 43, 28);

    if (unpack28(c28a, 0, i3, store, data->rr73_call, &ignored_kind, &out->has_unresolved_hash) < 0 ||
        unpack28(c28b, 0, i3, store, data->report_call, &ignored_kind, &out->has_unresolved_hash) < 0)
        return FT8_PROTOCOL_CODEC_MALFORMED;

    resolve_hash(store, FT8_HASH_10_BITS, h10, data->fox_call, &out->has_unresolved_hash);
    data->report_db = (int8_t)((int)r5 - 30);
    format_signed_dd(report, data->report_db);

    (void)snprintf(out->canonical_text, sizeof(out->canonical_text),
                   "%s RR73; %s %s %s",
                   data->rr73_call, data->report_call, data->fox_call, report);
    return FT8_PROTOCOL_CODEC_OK;
}

static Ft8ProtocolCodecStatus decode_free_text(const uint8_t payload[FT8_PAYLOAD_BYTES],
                                               Ft8ProtocolMessage *out)
{
    uint8_t b71[FT8_PROTOCOL_TELEMETRY_BYTES];
    char chars[14];
    int idx;

    decode_telemetry_bytes(payload, b71);
    chars[13] = '\0';

    for (idx = 12; idx >= 0; --idx) {
        uint16_t rem = 0;
        size_t i;
        for (i = 0; i < FT8_PROTOCOL_TELEMETRY_BYTES; ++i) {
            rem = (uint16_t)((rem << 8) | b71[i]);
            b71[i] = (uint8_t)(rem / 42u);
            rem = (uint16_t)(rem % 42u);
        }
        chars[idx] = charn_local(rem, CHAR_TABLE_FULL);
    }

    trim_copy(out->data.free_text.text, sizeof(out->data.free_text.text), chars);
    copy_text(out->canonical_text, sizeof(out->canonical_text), out->data.free_text.text);
    return FT8_PROTOCOL_CODEC_OK;
}

static Ft8ProtocolCodecStatus decode_telemetry(const uint8_t payload[FT8_PAYLOAD_BYTES],
                                               Ft8ProtocolMessage *out)
{
    static const char hex[] = "0123456789ABCDEF";
    Ft8ProtocolTelemetry *data = &out->data.telemetry;
    size_t i;

    decode_telemetry_bytes(payload, data->bytes);
    for (i = 0; i < FT8_PROTOCOL_TELEMETRY_BYTES; ++i) {
        data->hex[i * 2u] = hex[data->bytes[i] >> 4];
        data->hex[i * 2u + 1u] = hex[data->bytes[i] & 0x0Fu];
    }
    data->hex[18] = '\0';
    copy_text(out->canonical_text, sizeof(out->canonical_text), data->hex);
    return FT8_PROTOCOL_CODEC_OK;
}

Ft8ProtocolCodecStatus ft8_protocol_decode(const Ft8DecodedPayload *decoded,
                                           Ft8HashStore *hash_store,
                                           Ft8ProtocolMessage *out_message)
{
    Ft8ProtocolCodecStatus status;

    if (decoded == NULL || out_message == NULL)
        return FT8_PROTOCOL_CODEC_ERR_INVALID;

    memset(out_message, 0, sizeof(*out_message));
    memcpy(out_message->payload, decoded->payload, FT8_PAYLOAD_BYTES);
    out_message->candidate = decoded->candidate;
    out_message->ldpc_errors = decoded->ldpc_errors;
    out_message->crc_extracted = decoded->crc_extracted;
    out_message->crc_calculated = decoded->crc_calculated;
    out_message->type = ft8_protocol_get_type(decoded->payload);

    switch (out_message->type) {
    case FT8_PROTOCOL_STANDARD:
        status = decode_standard(decoded->payload, hash_store, out_message);
        break;
    case FT8_PROTOCOL_NONSTD_CALL:
        status = decode_nonstandard(decoded->payload, hash_store, out_message);
        break;
    case FT8_PROTOCOL_FREE_TEXT:
        status = decode_free_text(decoded->payload, out_message);
        break;
    case FT8_PROTOCOL_DXPEDITION:
        status = decode_dxpedition(decoded->payload, hash_store, out_message);
        break;
    case FT8_PROTOCOL_ARRL_FD:
        status = decode_arrl_fd(decoded->payload, hash_store, out_message);
        break;
    case FT8_PROTOCOL_TELEMETRY:
        status = decode_telemetry(decoded->payload, out_message);
        break;
    case FT8_PROTOCOL_EU_VHF:
    case FT8_PROTOCOL_ARRL_RTTY:
    case FT8_PROTOCOL_WWROF:
    case FT8_PROTOCOL_UNKNOWN:
    default:
        status = FT8_PROTOCOL_CODEC_UNSUPPORTED;
        break;
    }

    if (status == FT8_PROTOCOL_CODEC_OK)
        out_message->parse_status = FT8_PROTOCOL_PARSE_OK;
    else if (status == FT8_PROTOCOL_CODEC_UNSUPPORTED)
        out_message->parse_status = FT8_PROTOCOL_PARSE_UNSUPPORTED;
    else
        out_message->parse_status = FT8_PROTOCOL_PARSE_MALFORMED;

    return status;
}

void ft8_protocol_slot_init(Ft8ProtocolSlot *slot,
                            int64_t slot_id,
                            Ft8ProtocolMessage *message_storage,
                            size_t capacity)
{
    if (slot == NULL)
        return;

    slot->slot_id = slot_id;
    slot->status = FT8_PROTOCOL_SLOT_EMPTY;
    slot->messages = message_storage;
    slot->capacity = message_storage != NULL ? capacity : 0u;
    slot->message_count = 0u;
}

Ft8ProtocolSlotAddStatus ft8_protocol_slot_add_unique(Ft8ProtocolSlot *slot,
                                                     const Ft8ProtocolMessage *message)
{
    size_t i;

    if (slot == NULL || message == NULL ||
        (slot->capacity > 0u && slot->messages == NULL))
        return FT8_PROTOCOL_SLOT_ERR_INVALID;

    for (i = 0; i < slot->message_count; ++i) {
        if (memcmp(slot->messages[i].payload, message->payload, FT8_PAYLOAD_BYTES) == 0)
            return FT8_PROTOCOL_SLOT_DUPLICATE;
    }

    if (slot->message_count >= slot->capacity) {
        slot->status = FT8_PROTOCOL_SLOT_FULL;
        return FT8_PROTOCOL_SLOT_ERR_FULL;
    }

    slot->messages[slot->message_count++] = *message;
    slot->status = FT8_PROTOCOL_SLOT_OK;
    return FT8_PROTOCOL_SLOT_ADDED;
}

Ft8ProtocolSlotAddStatus ft8_protocol_slot_decode_add(Ft8ProtocolSlot *slot,
                                                     const Ft8DecodedPayload *decoded,
                                                     Ft8HashStore *hash_store,
                                                     Ft8ProtocolCodecStatus *out_codec_status)
{
    Ft8ProtocolMessage message;
    Ft8ProtocolCodecStatus codec_status;

    if (slot == NULL || decoded == NULL)
        return FT8_PROTOCOL_SLOT_ERR_INVALID;

    codec_status = ft8_protocol_decode(decoded, hash_store, &message);
    if (out_codec_status != NULL)
        *out_codec_status = codec_status;
    if (codec_status == FT8_PROTOCOL_CODEC_ERR_INVALID)
        return FT8_PROTOCOL_SLOT_ERR_INVALID;

    return ft8_protocol_slot_add_unique(slot, &message);
}

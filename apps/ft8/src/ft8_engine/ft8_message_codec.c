#include "../../include/ft8/cq_token.h"
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
    /* CQ has no hashed destination; its n12 bits carry no callsign. */
    hashed[0] = '\0';
    if (!icq) resolve_hash(store, FT8_HASH_12_BITS, n12, hashed, &out->has_unresolved_hash);

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

/* TX packing adapted from MiniFT8-V2 message.c at
 * 491e757ae6b1e4cfd2b9a6ba10f48b35643849e0. Reuse RX alphabets/section tables;
 * including deterministic hash fallback without persistent TX state. */
static void set_bits_be(uint8_t *bytes, unsigned start, unsigned count, uint32_t value)
{
    for (unsigned i = 0; i < count; ++i) {
        unsigned bit = start + i;
        if ((value >> (count - i - 1u)) & 1u)
            bytes[bit / 8u] |= (uint8_t)(0x80u >> (bit % 8u));
    }
}

static int32_t pack_basecall(const char *call, size_t length)
{
    char c6[6] = {' ', ' ', ' ', ' ', ' ', ' '};
    if (length < 3u || length > 7u) return -1;
    if (length > 4u && strncmp(call, "3DA0", 4) == 0) {
        memcpy(c6, "3D0", 3);
        memcpy(c6 + 3, call + 4, length - 4);
    } else if (strncmp(call, "3X", 2) == 0 && call[2] >= 'A' && call[2] <= 'Z') {
        c6[0] = 'Q';
        memcpy(c6 + 1, call + 2, length - 2);
    } else if (length <= 6u && call[2] >= '0' && call[2] <= '9') {
        memcpy(c6, call, length);
    } else if (length <= 5u && call[1] >= '0' && call[1] <= '9') {
        memcpy(c6 + 1, call, length);
    } else {
        return -1;
    }
    static const CharTable tables[6] = {CHAR_TABLE_ALPHANUM_SPACE, CHAR_TABLE_ALPHANUM,
        CHAR_TABLE_NUMERIC, CHAR_TABLE_LETTERS_SPACE, CHAR_TABLE_LETTERS_SPACE, CHAR_TABLE_LETTERS_SPACE};
    static const int radix[6] = {1, 36, 10, 27, 27, 27};
    int32_t value = 0;
    for (unsigned i = 0; i < 6; ++i) {
        int digit = nchar_local(c6[i], tables[i]);
        if (digit < 0) return -1;
        value = value * radix[i] + digit;
    }
    return value;
}

static Ft8ProtocolCodecStatus pack_call(const char call[FT8_PROTOCOL_CALL_CAP],
                                        bool allow_cq, uint32_t *value, char *suffix)
{
    if (!memchr(call, '\0', FT8_PROTOCOL_CALL_CAP) || !*call)
        return FT8_PROTOCOL_CODEC_MALFORMED;
    *suffix = '\0';
    size_t length = strlen(call);
    if (allow_cq && strcmp(call, "CQ") == 0) { *value = 2; return FT8_PROTOCOL_CODEC_OK; }
    if (allow_cq && strncmp(call, "CQ ", 3) == 0 && length >= 4u && length <= 7u) {
        if (!ft8_cq_modifier_pack(call + 3, length - 3, value))
            return FT8_PROTOCOL_CODEC_MALFORMED;
        return FT8_PROTOCOL_CODEC_OK;
    }
    if (length > 2u && call[length - 2] == '/' &&
        (call[length - 1] == 'P' || call[length - 1] == 'R')) {
        *suffix = call[length - 1];
        length -= 2;
    }
    int32_t base = pack_basecall(call, length);
    for (size_t i = 0; i < length; ++i)
        if (!((call[i] >= 'A' && call[i] <= 'Z') ||
              (call[i] >= '0' && call[i] <= '9'))) base = -1;
    if (base >= 0) {
        *value = FT8_NTOKENS + FT8_MAX22 + (uint32_t)base;
    } else {
        length = strlen(call);
        if (length < 3 || length > 11) return FT8_PROTOCOL_CODEC_UNSUPPORTED;
        for (size_t i = 0; i < length; ++i)
            if (!((call[i] >= 'A' && call[i] <= 'Z') ||
                  (call[i] >= '0' && call[i] <= '9') || call[i] == '/'))
                return FT8_PROTOCOL_CODEC_UNSUPPORTED;
        uint32_t hash;
        if (ft8_protocol_callsign_hash22(call, &hash) != FT8_PROTOCOL_CODEC_OK)
            return FT8_PROTOCOL_CODEC_UNSUPPORTED;
        *suffix = '\0';
        *value = FT8_NTOKENS + hash;
    }
    return FT8_PROTOCOL_CODEC_OK;
}

static bool pack_extra(const Ft8ProtocolStandard *standard, uint16_t *out)
{
    const char *extra = standard->extra;
    if (!memchr(extra, '\0', sizeof(standard->extra))) return false;
    size_t length = strlen(extra);
    if (standard->extra_kind == FT8_PROTOCOL_FIELD_NONE && !length) {
        *out = FT8_MAXGRID4 + 1u; return true;
    }
    if (standard->extra_kind == FT8_PROTOCOL_FIELD_TOKEN) {
        if (strcmp(extra, "RRR") == 0) *out = FT8_MAXGRID4 + 2u;
        else if (strcmp(extra, "RR73") == 0) *out = FT8_MAXGRID4 + 3u;
        else if (strcmp(extra, "73") == 0) *out = FT8_MAXGRID4 + 4u;
        else return false;
        return true;
    }
    if (standard->extra_kind == FT8_PROTOCOL_FIELD_GRID) {
        if (length != 4 || extra[0] < 'A' || extra[0] > 'R' || extra[1] < 'A' || extra[1] > 'R' ||
            extra[2] < '0' || extra[2] > '9' || extra[3] < '0' || extra[3] > '9') return false;
        *out = (uint16_t)((((extra[0] - 'A') * 18 + extra[1] - 'A') * 10 +
                          extra[2] - '0') * 10 + extra[3] - '0');
        return true;
    }
    if (standard->extra_kind != FT8_PROTOCOL_FIELD_REPORT) return false;
    bool roger = *extra == 'R';
    if (roger) ++extra;
    char sign = *extra++;
    if (sign != '+' && sign != '-') return false;
    unsigned count = 0;
    int report = 0;
    while (*extra >= '0' && *extra <= '9' && count < 2) {
        report = report * 10 + (*extra++ - '0'); ++count;
    }
    if (!count || *extra) return false;
    if (sign == '-') report = -report;
    /* Lower values collide with grid/terminal tokens in the pinned V2 packer. */
    if (report < -30) return false;
    *out = (uint16_t)((FT8_MAXGRID4 + 35 + report) | (roger ? 0x8000u : 0u));
    return true;
}

static Ft8ProtocolCodecStatus encode_standard(const Ft8ProtocolStandard *standard, uint8_t *payload)
{
    uint32_t to, de;
    char suffix_to, suffix_de;
    Ft8ProtocolCodecStatus status = pack_call(standard->call_to, true, &to, &suffix_to);
    if (status != FT8_PROTOCOL_CODEC_OK) return status;
    status = pack_call(standard->call_de, false, &de, &suffix_de);
    if (status != FT8_PROTOCOL_CODEC_OK) return status;
    if ((suffix_to == 'P' && suffix_de == 'R') || (suffix_to == 'R' && suffix_de == 'P'))
        return FT8_PROTOCOL_CODEC_UNSUPPORTED;
    uint16_t extra;
    if (!pack_extra(standard, &extra)) return FT8_PROTOCOL_CODEC_MALFORMED;
    set_bits_be(payload, 0, 29, (to << 1) | (suffix_to != '\0'));
    set_bits_be(payload, 29, 29, (de << 1) | (suffix_de != '\0'));
    set_bits_be(payload, 58, 16, extra);
    set_bits_be(payload, 74, 3, suffix_to == 'P' || suffix_de == 'P' ? 2u : 1u);
    return FT8_PROTOCOL_CODEC_OK;
}

static Ft8ProtocolCodecStatus encode_arrl_fd(const Ft8ProtocolArrlFd *fd, uint8_t *payload)
{
    uint32_t to, de;
    char suffix_to, suffix_de;
    Ft8ProtocolCodecStatus status = pack_call(fd->call_to, false, &to, &suffix_to);
    if (status != FT8_PROTOCOL_CODEC_OK) return status;
    status = pack_call(fd->call_de, false, &de, &suffix_de);
    if (status != FT8_PROTOCOL_CODEC_OK) return status;
    if (suffix_to || suffix_de) return FT8_PROTOCOL_CODEC_UNSUPPORTED;
    if (fd->transmitter_count < 1 || fd->transmitter_count > 32 ||
        fd->class_letter < 'A' || fd->class_letter > 'F' ||
        !memchr(fd->section, '\0', sizeof(fd->section))) return FT8_PROTOCOL_CODEC_MALFORMED;
    unsigned section = 0;
    while (section < FT8_ARRL_SECTIONS_COUNT && strcmp(fd->section, k_arrl_sections[section]) != 0)
        ++section;
    if (section == FT8_ARRL_SECTIONS_COUNT) return FT8_PROTOCOL_CODEC_MALFORMED;
    set_bits_be(payload, 0, 28, to);
    set_bits_be(payload, 28, 28, de);
    set_bits_be(payload, 56, 1, fd->has_r);
    set_bits_be(payload, 57, 4, (fd->transmitter_count - 1u) % 16u);
    set_bits_be(payload, 61, 3, (unsigned)(fd->class_letter - 'A'));
    set_bits_be(payload, 64, 7, section);
    set_bits_be(payload, 71, 3, fd->transmitter_count <= 16 ? 3u : 4u);
    return FT8_PROTOCOL_CODEC_OK;
}

static bool nonstandard_call(const char call[FT8_PROTOCOL_CALL_CAP])
{
    if (!memchr(call, '\0', FT8_PROTOCOL_CALL_CAP)) return false;
    size_t length = strlen(call);
    if (length < 3 || length > 11) return false;
    for (size_t i = 0; i < length; ++i)
        if (!((call[i] >= 'A' && call[i] <= 'Z') ||
              (call[i] >= '0' && call[i] <= '9') || call[i] == '/')) return false;
    return true;
}

static Ft8ProtocolCodecStatus encode_nonstandard(const Ft8ProtocolNonstandard *data, uint8_t *payload)
{
    if ((unsigned)data->terminal > FT8_PROTOCOL_TERMINAL_73 ||
        (data->is_cq && (data->terminal != FT8_PROTOCOL_TERMINAL_NONE ||
         !memchr(data->call_to, '\0', sizeof(data->call_to)) || strcmp(data->call_to, "CQ") != 0)))
        return FT8_PROTOCOL_CODEC_MALFORMED;
    if (!nonstandard_call(data->call_de) || (!data->is_cq && !nonstandard_call(data->call_to)))
        return FT8_PROTOCOL_CODEC_UNSUPPORTED;
    /* Full typed calls use V2's default iflip=0: source full, destination hashed. */
    uint32_t hash = 0;
    if (!data->is_cq) (void)ft8_protocol_callsign_hash22(data->call_to, &hash);
    uint64_t full = 0;
    for (const char *p = data->call_de; *p; ++p)
        full = full * 38u + (unsigned)nchar_local(*p, CHAR_TABLE_ALPHANUM_SPACE_SLASH);
    set_bits_be(payload, 0, 12, hash >> 10);
    set_bits_be(payload, 12, 26, (uint32_t)(full >> 32));
    set_bits_be(payload, 38, 32, (uint32_t)full);
    set_bits_be(payload, 71, 2, (unsigned)data->terminal);
    set_bits_be(payload, 73, 1, data->is_cq);
    set_bits_be(payload, 74, 3, 4);
    return FT8_PROTOCOL_CODEC_OK;
}

static Ft8ProtocolCodecStatus encode_free(const Ft8ProtocolFreeText *text, uint8_t *payload)
{
    if (!memchr(text->text, '\0', sizeof(text->text)) || !text->text[0])
        return FT8_PROTOCOL_CODEC_MALFORMED;
    size_t length = strlen(text->text);
    uint8_t b71[9] = {0};
    for (size_t i = 0; i < 13; ++i) {
        int digit = nchar_local(i < length ? text->text[i] : ' ', CHAR_TABLE_FULL);
        if (digit < 0) return FT8_PROTOCOL_CODEC_MALFORMED;
        unsigned carry = (unsigned)digit;
        for (int j = 8; j >= 0; --j) {
            carry += b71[j] * 42u;
            b71[j] = (uint8_t)carry;
            carry >>= 8;
        }
    }
    for (unsigned i = 0; i < 9; ++i)
        payload[i] = (uint8_t)((b71[i] << 1) | (i < 8 ? b71[i + 1] >> 7 : 0));
    return FT8_PROTOCOL_CODEC_OK;
}

Ft8ProtocolCodecStatus ft8_protocol_encode(const Ft8ProtocolMessage *message,
                                           uint8_t out_payload[FT8_PAYLOAD_BYTES])
{
    if (!out_payload) return FT8_PROTOCOL_CODEC_ERR_INVALID;
    memset(out_payload, 0, FT8_PAYLOAD_BYTES);
    if (!message) return FT8_PROTOCOL_CODEC_ERR_INVALID;
    uint8_t payload[FT8_PAYLOAD_BYTES] = {0};
    Ft8ProtocolCodecStatus status;
    switch (message->type) {
    case FT8_PROTOCOL_STANDARD: status = encode_standard(&message->data.standard, payload); break;
    case FT8_PROTOCOL_ARRL_FD: status = encode_arrl_fd(&message->data.arrl_fd, payload); break;
    case FT8_PROTOCOL_NONSTD_CALL: status = encode_nonstandard(&message->data.nonstandard, payload); break;
    case FT8_PROTOCOL_FREE_TEXT: status = encode_free(&message->data.free_text, payload); break;
    default: status = FT8_PROTOCOL_CODEC_UNSUPPORTED; break;
    }
    if (status == FT8_PROTOCOL_CODEC_OK) memcpy(out_payload, payload, sizeof(payload));
    return status;
}

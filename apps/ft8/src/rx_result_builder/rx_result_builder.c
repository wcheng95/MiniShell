#include "rx_result_builder.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static void copy_text(char *dst, size_t capacity, const char *src)
{
    if (dst == NULL || capacity == 0u)
        return;
    if (src == NULL)
        src = "";
    (void)snprintf(dst, capacity, "%s", src);
}

static void copy_upper(char *dst, size_t capacity, const char *src)
{
    size_t i = 0u;

    if (dst == NULL || capacity == 0u)
        return;
    if (src == NULL)
        src = "";

    while (src[i] != '\0' && i + 1u < capacity) {
        dst[i] = (char)toupper((unsigned char)src[i]);
        ++i;
    }
    dst[i] = '\0';
}

static int call_equals(const char *field, const char *local)
{
    size_t len;

    if (field == NULL || local == NULL || local[0] == '\0')
        return 0;

    if (strcmp(field, local) == 0)
        return 1;

    len = strlen(field);
    if (len >= 3u && field[0] == '<' && field[len - 1u] == '>') {
        size_t local_len = strlen(local);
        return local_len == len - 2u &&
               memcmp(field + 1, local, local_len) == 0;
    }
    return 0;
}

static int starts_cq_token(const char *field)
{
    return field != NULL &&
           (strcmp(field, "CQ") == 0 || strncmp(field, "CQ ", 3u) == 0);
}

static int valid_modifier(const char *text)
{
    size_t i;
    size_t len;
    int all_digits = 1;
    int all_letters = 1;

    if (text == NULL)
        return 0;
    len = strlen(text);
    if (len == 0u || len > 4u)
        return 0;

    for (i = 0u; i < len; ++i) {
        unsigned char c = (unsigned char)text[i];
        if (!isdigit(c))
            all_digits = 0;
        if (!(c >= 'A' && c <= 'Z'))
            all_letters = 0;
    }

    return (len == 3u && all_digits) || all_letters;
}

static int valid_callsign(const char *text)
{
    size_t i;
    size_t len;
    int has_letter = 0;
    int has_digit = 0;

    if (text == NULL)
        return 0;
    len = strlen(text);
    if (len < 3u || len >= RX_RESULT_CALL_CAP)
        return 0;

    for (i = 0u; i < len; ++i) {
        unsigned char c = (unsigned char)text[i];
        if (c >= 'A' && c <= 'Z')
            has_letter = 1;
        else if (isdigit(c))
            has_digit = 1;
        else if (c != '/')
            return 0;
    }
    return has_letter && has_digit;
}

static int valid_grid4(const char *text)
{
    return text != NULL && strlen(text) == 4u &&
           text[0] >= 'A' && text[0] <= 'R' &&
           text[1] >= 'A' && text[1] <= 'R' &&
           isdigit((unsigned char)text[2]) &&
           isdigit((unsigned char)text[3]);
}

/* V2-compatible normal FT8 report parser: optional R, optional sign, 0..30. */
static int parse_report(const char *text, int *out_has_r, int8_t *out_report)
{
    size_t i = 0u;
    int negative = 0;
    int value = 0;

    if (text == NULL || out_has_r == NULL || out_report == NULL || text[0] == '\0')
        return 0;

    *out_has_r = 0;
    if (text[i] == 'R') {
        *out_has_r = 1;
        ++i;
    }
    if (text[i] == '+') {
        ++i;
    } else if (text[i] == '-') {
        negative = 1;
        ++i;
    }
    if (text[i] == '\0' || !isdigit((unsigned char)text[i]))
        return 0;

    while (text[i] != '\0' && isdigit((unsigned char)text[i])) {
        value = value * 10 + (text[i] - '0');
        ++i;
    }
    if (text[i] != '\0' || value > 30)
        return 0;

    *out_report = (int8_t)(negative ? -value : value);
    return 1;
}

static size_t split_fields(char *buffer, char **tokens, size_t capacity)
{
    size_t count = 0u;
    char *p = buffer;

    while (*p != '\0') {
        while (*p == ' ')
            ++p;
        if (*p == '\0')
            break;
        if (count == capacity)
            return capacity + 1u;

        tokens[count++] = p;
        while (*p != '\0' && *p != ' ')
            ++p;
        if (*p == ' ')
            *p++ = '\0';
    }
    return count;
}

/*
 * Locked V3 exception: FREE_TEXT may additionally be a logical CQ only for
 * "CQ <nnn|AAAA> <valid-callsign> [grid]". Protocol type remains FREE_TEXT.
 */
static int classify_free_text_cq(const char *text,
                                 char call_de[RX_RESULT_CALL_CAP],
                                 char extra[RX_RESULT_EXTRA_CAP])
{
    char copy[RX_RESULT_TEXT_CAP];
    char *tokens[5] = {0};
    size_t count;

    copy_text(copy, sizeof(copy), text);
    count = split_fields(copy, tokens, 5u);

    if (count < 3u || count > 4u || strcmp(tokens[0], "CQ") != 0 ||
        !valid_modifier(tokens[1]) || !valid_callsign(tokens[2]))
        return 0;
    if (count == 4u && !valid_grid4(tokens[3]))
        return 0;

    copy_text(call_de, RX_RESULT_CALL_CAP, tokens[2]);
    if (count == 4u)
        copy_text(extra, RX_RESULT_EXTRA_CAP, tokens[3]);
    return 1;
}

static void copy_common(RxMessage *out, const Ft8ProtocolMessage *in)
{
    memcpy(out->payload, in->payload, FT8_PAYLOAD_BYTES);
    out->protocol_type = in->type;
    out->parse_status = in->parse_status;
    out->has_unresolved_hash = in->has_unresolved_hash;
    out->report_db = RX_RESULT_REPORT_UNKNOWN;
    copy_text(out->canonical_text, sizeof(out->canonical_text), in->canonical_text);
    out->offset_hz = in->offset_hz;
    out->snr_db = in->snr_db;
    out->candidate = in->candidate;
    out->ldpc_errors = in->ldpc_errors;
    out->crc_extracted = in->crc_extracted;
    out->crc_calculated = in->crc_calculated;
}

static void classify_standard_qso(const Ft8ProtocolStandard *standard,
                                  RxMessage *out)
{
    int has_r;
    int8_t report;

    switch (standard->extra_kind) {
    case FT8_PROTOCOL_FIELD_GRID:
        /* Preserve V2: "R FN42" is not treated as an ordinary TX1 grid. */
        if (valid_grid4(standard->extra))
            out->qso_kind = RX_QSO_MSG_TX1;
        break;

    case FT8_PROTOCOL_FIELD_REPORT:
        if (parse_report(standard->extra, &has_r, &report)) {
            out->qso_kind = has_r ? RX_QSO_MSG_TX3 : RX_QSO_MSG_TX2;
            out->report_db = report;
        }
        break;

    case FT8_PROTOCOL_FIELD_TOKEN:
        if (strcmp(standard->extra, "RRR") == 0 ||
            strcmp(standard->extra, "RR73") == 0) {
            out->qso_kind = RX_QSO_MSG_TX4;
        } else if (strcmp(standard->extra, "73") == 0) {
            out->qso_kind = RX_QSO_MSG_TX5;
        }
        break;

    default:
        break;
    }
}

static void classify_message(const RxResultBuilder *builder,
                             const Ft8ProtocolMessage *in,
                             RxMessage *out)
{
    const char *local = builder->config.local_callsign;

    switch (in->type) {
    case FT8_PROTOCOL_STANDARD:
        copy_text(out->call_to, sizeof(out->call_to), in->data.standard.call_to);
        copy_text(out->call_de, sizeof(out->call_de), in->data.standard.call_de);
        copy_text(out->extra, sizeof(out->extra), in->data.standard.extra);
        out->is_cq = starts_cq_token(in->data.standard.call_to) != 0;
        if (!out->is_cq)
            out->is_to_me = call_equals(in->data.standard.call_to, local) != 0;
        classify_standard_qso(&in->data.standard, out);
        break;

    case FT8_PROTOCOL_NONSTD_CALL:
        copy_text(out->call_to, sizeof(out->call_to), in->data.nonstandard.call_to);
        copy_text(out->call_de, sizeof(out->call_de), in->data.nonstandard.call_de);
        out->is_cq = in->data.nonstandard.is_cq;
        if (!out->is_cq)
            out->is_to_me = call_equals(in->data.nonstandard.call_to, local) != 0;
        if (in->data.nonstandard.terminal == FT8_PROTOCOL_TERMINAL_RRR ||
            in->data.nonstandard.terminal == FT8_PROTOCOL_TERMINAL_RR73) {
            out->qso_kind = RX_QSO_MSG_TX4;
        } else if (in->data.nonstandard.terminal == FT8_PROTOCOL_TERMINAL_73) {
            out->qso_kind = RX_QSO_MSG_TX5;
        }
        break;

    case FT8_PROTOCOL_ARRL_FD:
        copy_text(out->call_to, sizeof(out->call_to), in->data.arrl_fd.call_to);
        copy_text(out->call_de, sizeof(out->call_de), in->data.arrl_fd.call_de);
        copy_text(out->extra, sizeof(out->extra), in->data.arrl_fd.section);
        out->is_to_me = call_equals(in->data.arrl_fd.call_to, local) != 0;
        /* Field Day stage/exchange classification is intentionally AS-6. */
        break;

    case FT8_PROTOCOL_DXPEDITION:
        out->is_to_me = call_equals(in->data.dxpedition.rr73_call, local) != 0 ||
                        call_equals(in->data.dxpedition.report_call, local) != 0;
        /* DXpedition has different two-message semantics; do not flatten here. */
        break;

    case FT8_PROTOCOL_FREE_TEXT:
        out->is_cq = classify_free_text_cq(in->canonical_text,
                                           out->call_de,
                                           out->extra) != 0;
        break;

    default:
        break;
    }
}

RxResultBuilderConfig rx_result_builder_default_config(void)
{
    RxResultBuilderConfig config;
    memset(&config, 0, sizeof(config));
    return config;
}

RxResultStatus rx_result_builder_init(RxResultBuilder *builder,
                                      const RxResultBuilderConfig *config)
{
    if (builder == NULL || config == NULL)
        return RX_RESULT_ERR_INVALID;

    memset(builder, 0, sizeof(*builder));
    copy_upper(builder->config.local_callsign,
               sizeof(builder->config.local_callsign),
               config->local_callsign);
    builder->initialized = 1;
    return RX_RESULT_OK;
}

void rx_result_builder_destroy(RxResultBuilder *builder)
{
    if (builder != NULL)
        memset(builder, 0, sizeof(*builder));
}

RxResultStatus rx_result_builder_build(const RxResultBuilder *builder,
                                       const Ft8ProtocolSlot *slot,
                                       RxMessage *message_storage,
                                       size_t message_capacity,
                                       RxBatch *out_batch)
{
    size_t i;

    if (builder == NULL || !builder->initialized)
        return RX_RESULT_ERR_NOT_INITIALIZED;
    if (slot == NULL || out_batch == NULL ||
        (slot->message_count > 0u && (slot->messages == NULL || message_storage == NULL)))
        return RX_RESULT_ERR_INVALID;
    if (slot->message_count > message_capacity)
        return RX_RESULT_ERR_OUTPUT_FULL;

    memset(out_batch, 0, sizeof(*out_batch));
    out_batch->slot_id = slot->slot_id;
    out_batch->protocol_status = slot->status;
    out_batch->messages = message_storage;
    out_batch->capacity = message_capacity;
    out_batch->message_count = slot->message_count;

    for (i = 0u; i < slot->message_count; ++i) {
        RxMessage *out = &message_storage[i];
        memset(out, 0, sizeof(*out));
        copy_common(out, &slot->messages[i]);
        classify_message(builder, &slot->messages[i], out);
    }

    return RX_RESULT_OK;
}

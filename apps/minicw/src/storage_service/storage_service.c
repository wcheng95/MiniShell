/* Keyer storage policy; all resource access stays in the private port. */
#include "storage_service.h"
#include "minicw_port.h"
#include "minicw_ascii.h"
#include "minicw_libc.h"
#include <string.h>

static const char *const in_labels[] = {"Paddle-Normal", "Paddle-Reverse", "SK-Tip", "SK-Ring", "SK-Both"};
static const char *const out_labels[] = {"SK-Normal", "SK-Normal", "SK-Normal", "SK-Mono", "OFF"};
static const char *const paddle_labels[] = {"IambicA", "IambicB", "Bug"};
static bool equal_ci(const char *a, const char *b)
{
    while (*a && minicw_upper(*a) == minicw_upper(*b)) { ++a; ++b; }
    return minicw_upper(*a) == minicw_upper(*b);
}
static bool number(const char *s, unsigned low, unsigned high, unsigned *out)
{
    unsigned n = 0;
    if (!*s) return false;
    for (; *s; ++s) {
        if (*s < '0' || *s > '9' || n > high / 10) return false;
        n = n * 10 + (unsigned)(*s - '0');
        if (n > high) return false;
    }
    if (n < low) return false;
    *out = n; return true;
}
static bool mode(const char *s, const char *const *labels, unsigned count, unsigned *out)
{
    for (unsigned i = 0; i < count; ++i) if (equal_ci(s, labels[i])) { *out = i; return true; }
    /* Pinned Mini-CW settings aliases, including numeric modes. */
    static const char *const old_in[] = {"Pdl", "Pdl-R", "SK-T", "SK-R"};
    static const char *const old_out[] = {"Pdl", "Pdl-R", "SK", "SK-M", "OFF"};
    if (labels != paddle_labels) {
        const char *const *old = labels == in_labels ? old_in : old_out;
        unsigned old_count = labels == in_labels ? 4 : 5;
        for (unsigned i = 0; i < old_count; ++i) if (equal_ci(s, old[i])) { *out = i; return true; }
        if (labels == in_labels && equal_ci(s, "Paddle_Reverse")) { *out = 1; return true; }
    }
    if (labels == paddle_labels) {
        if (equal_ci(s, "Iambic-A")) { *out = 0; return true; }
        if (equal_ci(s, "Iambic-B")) { *out = 1; return true; }
    } else {
        if (equal_ci(s, "Paddle")) { *out = 0; return true; }
        if (equal_ci(s, "PdlR") || equal_ci(s, "PaddleR") || equal_ci(s, "Paddle-R") || equal_ci(s, "Paddle_R")) { *out = 1; return true; }
        if (labels == in_labels && (equal_ci(s, "SKT") || equal_ci(s, "SK"))) { *out = 2; return true; }
        if (equal_ci(s, "SK-Mono") || equal_ci(s, "SKMono") ||
            (labels == in_labels && (equal_ci(s, "SKR") || equal_ci(s, "SK_Mono"))) ||
            (labels == out_labels && equal_ci(s, "SKM"))) { *out = 3; return true; }
    }
    return number(s, 0, count - 1, out);
}
void storage_defaults(storage_snapshot_t *out)
{
    *out = (storage_snapshot_t){.volume = 80, .tone_hz = 700,
        .key_in = KEYER_KEY_IN_PADDLE, .key_in_wpm = 19,
        .keyer = {.key_out_mode = KEYER_KEY_OUT_SK, .paddle_mode = KEYER_PADDLE_IAMBIC_A,
            .sk_wpm = 19, .tune_timeout_s = 10, .repeat_interval_s = 6,
            .mycall = "AG6AQ", .message = {"CQ POTA"}}};
}
static char *trim(char *s)
{
    while (*s == ' ' || *s == '\t') ++s;
    size_t n = strlen(s);
    while (n && (s[n-1] == ' ' || s[n-1] == '\t')) s[--n] = 0;
    return s;
}
static bool text_value(char *out, const char *s, size_t max, bool call)
{
    size_t n = strlen(s);
    if (n > max) return false;
    for (size_t i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)s[i];
        if (call ? !((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '/') : (c < 32 || c > 126)) return false;
    }
    memcpy(out, s, n + 1); return true;
}
static bool field(storage_snapshot_t *s, unsigned section, const char *key, char *value)
{
    unsigned n;
    /* Message spaces and every '=' after the first are literal data. */
    if (section == 2 && (key[0] == 'm' || key[0] == 'M') && key[1] >= '1' && key[1] <= '5' && !key[2])
        return text_value(s->keyer.message[key[1]-'1'], value, KEYER_MESSAGE_MAX_LEN, false);
    value = trim(value);
#define NUM(name, target, lo, hi) if (equal_ci(key, name)) { if (!number(value, lo, hi, &n)) return false; target = n; return true; }
    if (section == 1) {
        NUM("volume", s->volume, 0, 99)
        NUM("tone_hz", s->tone_hz, 300, 999)
        NUM("key_in_wpm", s->key_in_wpm, 5, 60)
        if (equal_ci(key, "key_in")) { if (!mode(value, in_labels, 5, &n)) return false; s->key_in = (keyer_key_in_mode_t)n; }
    } else if (section == 2) {
        NUM("sk_wpm", s->keyer.sk_wpm, 5, 60)
        NUM("tx_delay_s", s->keyer.tx_delay_s, 0, 99)
        NUM("tune_timeout_s", s->keyer.tune_timeout_s, 0, 20)
        NUM("repeat_interval_s", s->keyer.repeat_interval_s, 1, 99)
        if (equal_ci(key, "key_out")) { if (!mode(value, out_labels, 5, &n)) return false; s->keyer.key_out_mode = n < KEYER_KEY_OUT_SK ? KEYER_KEY_OUT_SK : (keyer_key_out_mode_t)n; }
        else if (equal_ci(key, "paddle")) { if (!mode(value, paddle_labels, 3, &n)) return false; s->keyer.paddle_mode = (keyer_paddle_mode_t)n; }
        else if (equal_ci(key, "mycall")) return text_value(s->keyer.mycall, value, KEYER_MYCALL_MAX_LEN, true);
    }
#undef NUM
    return true;
}
bool storage_parse(const char *text, storage_snapshot_t *out)
{
    storage_snapshot_t draft;
    storage_defaults(&draft);
    unsigned section = 0;
    while (*text) {
        char line[160]; size_t n = 0;
        while (*text && *text != '\n') {
            if (n + 1 == sizeof(line)) return false;
            line[n++] = *text++;
        }
        if (*text) ++text;
        if (n && line[n-1] == '\r') --n;
        line[n] = 0;
        char *p = line;
        while (*p == ' ' || *p == '\t') ++p;
        if (!*p || *p == '#' || *p == ';') continue;
        if (*p == '[') {
            p = trim(p); n = strlen(p);
            if (n < 2 || p[n-1] != ']') return false;
            p[n-1] = 0; p = trim(p+1);
            section = equal_ci(p, "system") ? 1 : equal_ci(p, "keyer") ? 2 : 0;
            continue;
        }
        if (!section) continue;
        char *eq = p;
        while (*eq && *eq != '=') ++eq;
        if (!*eq) return false;
        *eq++ = 0;
        if (!field(&draft, section, trim(p), eq)) return false;
    }
    *out = draft; return true;
}
bool storage_serialize(const storage_snapshot_t *s, char *out, size_t size)
{
    if ((unsigned)s->key_in >= 5 || (unsigned)s->keyer.key_out_mode >= 5 || (unsigned)s->keyer.paddle_mode >= 3) return false;
    int n = snprintf(out, size,
        "# Mini-CW Keyer settings\n\n[system]\nvolume=%u\ntone_hz=%u\nkey_in=%s\nkey_in_wpm=%u\n\n"
        "[keyer]\nkey_out=%s\npaddle=%s\nsk_wpm=%u\ntx_delay_s=%u\ntune_timeout_s=%u\nrepeat_interval_s=%u\n"
        "mycall=%s\nm1=%s\nm2=%s\nm3=%s\nm4=%s\nm5=%s\n",
        (unsigned)s->volume, (unsigned)s->tone_hz, in_labels[s->key_in], (unsigned)s->key_in_wpm,
        out_labels[s->keyer.key_out_mode], paddle_labels[s->keyer.paddle_mode], (unsigned)s->keyer.sk_wpm,
        (unsigned)s->keyer.tx_delay_s, (unsigned)s->keyer.tune_timeout_s, (unsigned)s->keyer.repeat_interval_s,
        s->keyer.mycall, s->keyer.message[0], s->keyer.message[1], s->keyer.message[2], s->keyer.message[3], s->keyer.message[4]);
    return n >= 0 && (size_t)n < size;
}
bool storage_equal(const storage_snapshot_t *a, const storage_snapshot_t *b)
{
    return a->volume == b->volume && a->tone_hz == b->tone_hz && a->key_in == b->key_in &&
        a->key_in_wpm == b->key_in_wpm && a->keyer.key_out_mode == b->keyer.key_out_mode &&
        a->keyer.paddle_mode == b->keyer.paddle_mode && a->keyer.sk_wpm == b->keyer.sk_wpm &&
        a->keyer.tx_delay_s == b->keyer.tx_delay_s && a->keyer.tune_timeout_s == b->keyer.tune_timeout_s &&
        a->keyer.repeat_interval_s == b->keyer.repeat_interval_s && !strcmp(a->keyer.mycall,b->keyer.mycall) &&
        !strcmp(a->keyer.message[0],b->keyer.message[0]) && !strcmp(a->keyer.message[1],b->keyer.message[1]) &&
        !strcmp(a->keyer.message[2],b->keyer.message[2]) && !strcmp(a->keyer.message[3],b->keyer.message[3]) &&
        !strcmp(a->keyer.message[4],b->keyer.message[4]);
}
storage_load_t storage_load(storage_snapshot_t *out)
{
    char text[4096];
    storage_defaults(out);
    minicw_file_result_t result = minicw_port_file_read("/flash/minicw/setting.txt", text, sizeof(text));
    if (result == MINICW_FILE_MISSING) return STORAGE_MISSING;
    if (result == MINICW_FILE_INVALID) return STORAGE_INVALID;
    if (result != MINICW_FILE_OK) return STORAGE_READ_FAILED;
    return storage_parse(text, out) ? STORAGE_OK : STORAGE_INVALID;
}
bool storage_save(const storage_snapshot_t *snapshot)
{
    char text[1024];
    return storage_serialize(snapshot, text, sizeof(text)) && minicw_port_file_replace("/flash/minicw", "/flash/minicw/setting.tmp", "/flash/minicw/setting.txt", text, strlen(text));
}

typedef struct {
    char line[128];
    size_t length, count, capacity;
    keyer_op_entry_t *entries;
    bool overflow, failed;
} op_parser_t;

static void op_line(op_parser_t *parser)
{
    size_t length = parser->length;
    if (parser->overflow) return;
    if (length && parser->line[length - 1] == '\r') --length;
    parser->line[length] = 0;
    char *call = trim(parser->line);
    if (!*call || *call == '#' || *call == ';') return;
    char *name = call;
    while (*name && *name != ',') ++name;
    if (!*name) return;
    *name++ = 0;
    call = trim(call); name = trim(name);
    /* Keep the standalone optional header handling and duplicate order. */
    if (equal_ci(call, "call") && equal_ci(name, "name")) return;
    size_t call_len = strlen(call), name_len = strlen(name);
    if (!call_len || call_len > KEYER_OP_CALL_MAX_LEN ||
        !name_len || name_len > KEYER_OP_NAME_MAX_LEN) return;
    bool valid = true;
    for (size_t i = 0; i < call_len; ++i) {
        unsigned char ch = (unsigned char)call[i];
        if (!minicw_alnum(ch)) valid = false;
        call[i] = (char)minicw_upper(ch);
    }
    for (size_t i = 0; i < name_len; ++i) {
        unsigned char ch = (unsigned char)name[i];
        if (ch < 32 || ch > 126 || ch == ',') valid = false;
    }
    if (!valid) return;
    if (parser->count == parser->capacity) {
        /* Memory API byte counts are uint32_t; guard doubling and multiplication. */
        if (parser->capacity > UINT32_MAX / sizeof(keyer_op_entry_t) / 2U) {
            parser->failed = true; return;
        }
        size_t capacity = parser->capacity ? parser->capacity * 2U : 64U;
        void *table = parser->entries;
        if (!minicw_port_memory_resize(&table, (uint32_t)(capacity * sizeof(keyer_op_entry_t)))) {
            parser->failed = true; return;
        }
        parser->entries = table;
        parser->capacity = capacity;
    }
    memcpy(parser->entries[parser->count].call, call, call_len + 1);
    memcpy(parser->entries[parser->count].name, name, name_len + 1);
    ++parser->count;
}
static bool op_byte(op_parser_t *parser, char ch)
{
    if (!ch) return false;
    if (ch == '\n') {
        op_line(parser);
        parser->length = 0;
        parser->overflow = false;
    } else if (parser->length + 1 < sizeof(parser->line)) {
        parser->line[parser->length++] = ch;
    } else parser->overflow = true;
    return !parser->failed;
}
void storage_op_free(keyer_op_entry_t *entries)
{
    minicw_port_memory_release(entries);
}
storage_op_result_t storage_op_load(keyer_op_entry_t **entries, size_t *count)
{
    op_parser_t parser = {0};
    char bytes[128];
    minicw_read_stream_t stream = NULL;
    *entries = NULL; *count = 0;
    minicw_file_result_t opened = minicw_port_read_open("/flash/minicw/qsocalls.csv", &stream);
    if (opened == MINICW_FILE_MISSING) return STORAGE_OP_MISSING;
    if (opened != MINICW_FILE_OK) return STORAGE_OP_FAILED;
    bool valid = true;
    for (;;) {
        uint32_t got = 0;
        if (!minicw_port_read_next(stream, bytes, sizeof(bytes), &got)) { valid = false; break; }
        if (!got) break;
        for (uint32_t i = 0; i < got; ++i) {
            if (!op_byte(&parser, bytes[i])) { valid = false; break; }
        }
        if (!valid) break;
    }
    if (valid) { op_line(&parser); valid = !parser.failed; } /* Final line without LF. */
    /* Never publish a partially trusted table, including close failures. */
    if (!minicw_port_read_close(stream)) valid = false;
    if (!valid) { storage_op_free(parser.entries); return STORAGE_OP_FAILED; }
    *entries = parser.entries; *count = parser.count;
    return STORAGE_OP_OK;
}

bool storage_transcript_append(uint32_t date, const char *line)
{
    char path[] = "/flash/minicw/00000000.txt";
    for (unsigned i = 0; i < 8; ++i) {
        path[sizeof("/flash/minicw/") - 1 + 7 - i] = (char)('0' + date % 10U); date /= 10U;
    }
    return minicw_port_file_append("/flash/minicw", path, line, (uint32_t)strlen(line));
}

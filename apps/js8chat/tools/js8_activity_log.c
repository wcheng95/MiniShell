#include "js8_activity_log.h"
#include <inttypes.h>
#include <stdarg.h>
#include <string.h>

static unsigned leap(unsigned year)
{
    return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}
static unsigned month_days(unsigned year, unsigned month)
{
    static const unsigned days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    return days[month-1] + (month == 2 ? leap(year) : 0);
}
int js8_log_parse_dial(const char *text, int64_t *out)
{
    if (!text || !*text || !out) return -1;
    /* Reserve enough headroom for every signed 32-bit audio frequency. */
    const uint64_t limit = (INT64_MAX - INT32_MAX) / 1000;
    uint64_t value = 0;
    for (const char *p = text; *p; ++p) {
        if (*p < '0' || *p > '9' || value > (limit - (unsigned)(*p-'0'))/10) return -1;
        value = value*10 + (unsigned)(*p-'0');
    }
    *out = (int64_t)value;
    return 0;
}
int js8_log_parse_utc(const char *text, uint64_t *out)
{
    if (!text || !out || strlen(text) != 16 || text[8] != 'T' || text[15] != 'Z') return -1;
    unsigned v[6] = {0};
    const unsigned starts[] = {0,4,6,9,11,13}, widths[] = {4,2,2,2,2,2};
    for (unsigned i = 0; i < 6; ++i)
        for (unsigned n = 0; n < widths[i]; ++n) {
            char c = text[starts[i]+n];
            if (c < '0' || c > '9') return -1;
            v[i] = v[i]*10 + (unsigned)(c-'0');
        }
    if (!v[0] || v[1] < 1 || v[1] > 12 || v[2] < 1 ||
        v[2] > month_days(v[0], v[1]) || v[3] > 23 || v[4] > 59 || v[5] > 59 || v[5] % 15)
        return -1;
    uint64_t days = 0;
    for (unsigned y = 1; y < v[0]; ++y) days += 365 + leap(y);
    for (unsigned m = 1; m < v[1]; ++m) days += month_days(v[0], m);
    *out = (days + v[2]-1)*86400 + v[3]*3600 + v[4]*60 + v[5];
    return 0;
}
int js8_log_format_utc(uint64_t seconds, char out[21])
{
    if (!out || seconds >= UINT64_C(315537897600)) return -1; /* year 10000 */
    uint64_t days = seconds/86400;
    unsigned year = 1, month = 1;
    while (days >= 365 + leap(year)) { days -= 365 + leap(year); ++year; }
    while (days >= month_days(year, month)) { days -= month_days(year, month); ++month; }
    char formatted[64];
    snprintf(formatted, sizeof(formatted), "%04u-%02u-%02uT%02u:%02u:%02uZ", year, month, (unsigned)days+1,
             (unsigned)(seconds/3600%24), (unsigned)(seconds/60%60), (unsigned)(seconds%60));
    memcpy(out, formatted, 21);
    return 0;
}

void js8_log_open(Js8ActivityLog *log, const char *path, const Js8LogMetadata *metadata)
{
    memset(log, 0, sizeof(*log));
    log->metadata = *metadata;
    log->file = fopen(path, "ab");
    if (!log->file) log->error = "open";
}
/* 1023 bytes expanding to six ASCII bytes each plus bounded fields/metadata. */
typedef struct { char bytes[8192]; size_t used; int failed; } Line;
static void append(Line *line, const char *format, ...)
{
    if (line->failed) return;
    va_list args;
    va_start(args, format);
    int n = vsnprintf(line->bytes + line->used, sizeof(line->bytes) - line->used, format, args);
    va_end(args);
    if (n < 0 || (size_t)n >= sizeof(line->bytes) - line->used) line->failed = 1;
    else line->used += (size_t)n;
}
static void string(Line *line, const char *bytes, size_t length)
{
    append(line, "\"");
    for (size_t i = 0; i < length; ++i) {
        unsigned c = (unsigned char)bytes[i];
        if (c == '"' || c == '\\') append(line, "\\%c", (int)c);
        else if (c < 32 || c >= 127) append(line, "\\u%04x", c);
        else append(line, "%c", (int)c);
    }
    append(line, "\"");
}
static void field(Line *line, const char *key, const char *value)
{
    append(line, ","); string(line, key, strlen(key)); append(line, ":");
    string(line, value, strlen(value));
}
void js8_log_event(Js8ActivityLog *log, const Js8Activity *event)
{
    if (log->error || !log->file) return;
    const Js8ActivityFields *f = &event->fields;
    static const char *const names[] = {"HB", "CQ", "COMPOUND", "DIRECTED", "DATA", "MESSAGE"};
    Line line = {0};
    append(&line, "{\"schema\":\"js8-activity-v1\"");
    field(&line, "event", names[f->kind]);
    append(&line, ",\"slot\":%" PRIu32 ",\"elapsed_s\":%" PRIu64
           ",\"audio_millihz\":%" PRId32 ",\"tx_flags\":%u,\"score\":%d,\"hard_errors\":%d",
           f->slot_index, event->elapsed_seconds, f->frequency_millihz, f->tx_flags, f->score, f->hard_errors);
    if (log->metadata.have_utc) {
        char utc[21];
        if (event->elapsed_seconds > UINT64_MAX - log->metadata.start_seconds ||
            js8_log_format_utc(log->metadata.start_seconds + event->elapsed_seconds, utc)) {
            log->error = "utc_range"; return;
        }
        field(&line, "utc", utc);
    }
    if (log->metadata.have_dial)
        append(&line, ",\"dial_hz\":%" PRId64 ",\"rf_millihz\":%" PRId64,
               log->metadata.dial_hz, log->metadata.dial_hz*1000 + f->frequency_millihz);
    switch (f->kind) {
    case JS8_ACTIVITY_HEARTBEAT:
    case JS8_ACTIVITY_CQ:
        field(&line, "call", f->call); field(&line, "grid", f->grid); field(&line, "beacon", f->beacon);
        append(&line, ",\"subtype\":%u", f->subtype);
        break;
    case JS8_ACTIVITY_COMPOUND:
        field(&line, "call", f->call); field(&line, "grid", f->grid);
        append(&line, ",\"extra\":%u,\"bits3\":%u,\"compound_directed\":%s",
               f->extra, f->bits3, f->compound_directed ? "true" : "false");
        break;
    case JS8_ACTIVITY_DIRECTED:
        field(&line, "from", f->from); field(&line, "to", f->to);
        append(&line, ",\"command_code\":%u", f->command_code); field(&line, "command", f->command);
        if (f->has_number) append(&line, ",\"number\":%d", f->number);
        append(&line, ",\"free_text\":%s,\"ack\":%s,\"end73\":%s",
               f->free_text ? "true" : "false", f->ack ? "true" : "false", f->end73 ? "true" : "false");
        break;
    case JS8_ACTIVITY_DATA:
        field(&line, "codec", f->codec == JS8_ACTIVITY_CODEC_HUFFMAN ? "huffman" :
              f->codec == JS8_ACTIVITY_CODEC_JSC ? "jsc" : "none");
        break;
    case JS8_ACTIVITY_MESSAGE:
        field(&line, "from", f->from); field(&line, "to", f->to);
        append(&line, ",\"first_slot\":%" PRIu32 ",\"last_slot\":%" PRIu32, f->first_slot, f->last_slot);
        break;
    }
    if (f->kind == JS8_ACTIVITY_DATA || f->kind == JS8_ACTIVITY_MESSAGE) {
        append(&line, ",\"text\":"); string(&line, event->text, event->text_len);
    }
    append(&line, "}\n");
    if (line.failed) log->error = "line_capacity";
    else if (fwrite(line.bytes, 1, line.used, log->file) != line.used) log->error = "write";
}
void js8_log_flush(Js8ActivityLog *log)
{
    if (log->file && fflush(log->file) && !log->error) log->error = "flush";
}
void js8_log_close(Js8ActivityLog *log)
{
    if (log->file && fclose(log->file) && !log->error) log->error = "close";
    log->file = NULL;
}

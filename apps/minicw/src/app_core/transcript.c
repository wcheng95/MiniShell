/* Chronological minute capture is RAM-only. app_core owns when disk is safe. */
#include "transcript.h"
#include "minicw_port.h"
#include "storage_service.h"
#include <string.h>
#define TRANSCRIPT_CAPACITY 1024U
typedef struct record {
    struct record *next;
    uint32_t date;
    char line[5 + TRANSCRIPT_CAPACITY + 8 + 1];
} transcript_record_t;
static transcript_record_t *s_head, *s_tail;
static char s_payload[TRANSCRIPT_CAPACITY + 1];
static uint32_t s_date;
static uint16_t s_minute, s_length;
static bool s_active, s_truncated;
static void reset_minute(void)
{
    s_active = s_truncated = false; s_length = 0; s_payload[0] = 0;
}
static void drain_record(bool persist)
{
    if (!s_head) return;
    transcript_record_t *record = s_head;
    s_head = record->next;
    if (!s_head) s_tail = NULL;
    /* Append has consumed its attempt, even if close is ambiguous. */
    if (persist) (void)storage_transcript_append(record->date, record->line);
    minicw_port_memory_release(record);
}
void transcript_drain(bool persist)
{
    while (s_head) drain_record(persist);
}
void transcript_drain_one(void) { drain_record(true); }
void transcript_init(void)
{
    transcript_drain(false);
    reset_minute();
}
void transcript_finalize(void)
{
    if (s_active && s_length) {
        void *memory = NULL;
        if (minicw_port_memory_resize(&memory, sizeof(transcript_record_t))) {
            transcript_record_t *record = memory;
            record->next = NULL; record->date = s_date;
            unsigned hour = s_minute / 60U, minute = s_minute % 60U;
            record->line[0] = (char)('0' + hour / 10U);
            record->line[1] = (char)('0' + hour % 10U);
            record->line[2] = (char)('0' + minute / 10U);
            record->line[3] = (char)('0' + minute % 10U);
            record->line[4] = ' ';
            memcpy(record->line + 5, s_payload, s_length);
            const char *suffix = s_truncated ? " [TRUNC]\n" : "\n";
            memcpy(record->line + 5 + s_length, suffix, s_truncated ? sizeof(" [TRUNC]\n") : sizeof("\n"));
            if (s_tail) s_tail->next = record; else s_head = record;
            s_tail = record;
        }
        /* Allocation failure drops only this record, not any queued minute. */
    }
    reset_minute();
}
static bool current_time(uint32_t *date, uint16_t *minute)
{
    if (!minicw_port_utc_minute(date, minute)) return false;
    if (s_active && (*date != s_date || *minute != s_minute)) transcript_finalize();
    return true;
}
void transcript_update(void)
{
    uint32_t date; uint16_t minute;
    (void)current_time(&date, &minute);
}
void transcript_append(char ch)
{
    if (ch == '\r' || ch == '\n') ch = ' ';
    if ((unsigned char)ch < 32 || (unsigned char)ch > 126) return;
    uint32_t date; uint16_t minute;
    if (!current_time(&date, &minute)) return;
    if (!s_active) { s_date = date; s_minute = minute; s_active = true; }
    if (s_length == TRANSCRIPT_CAPACITY) { s_truncated = true; return; }
    s_payload[s_length++] = ch; s_payload[s_length] = 0;
}
void transcript_text(const char *text)
{
    if (text) while (*text) transcript_append(*text++);
}
void transcript_backspace(void)
{
    uint32_t date; uint16_t minute;
    if (!current_time(&date, &minute) || !s_length) return;
    s_payload[--s_length] = 0;
    if (!s_length) reset_minute();
}

#include "js8_activity_log.h"
#include <string.h>

void js8_log_open(Js8ActivityLog *log, const char *path, const Js8LogMetadata *metadata)
{
    memset(log, 0, sizeof(*log));
    log->metadata = *metadata;
    log->file = fopen(path, "ab");
    if (!log->file) log->error = "open";
}
static int file_write(void *context, const char *bytes, size_t length)
{
    return fwrite(bytes, 1, length, context) == length ? 0 : -1;
}
void js8_log_event(Js8ActivityLog *log, const Js8Activity *event)
{
    if (!log->error && log->file)
        log->error = js8_activity_json(&log->metadata, event, file_write, log->file);
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

#ifndef JS8_ACTIVITY_LOG_H
#define JS8_ACTIVITY_LOG_H
#include "js8_activity.h"
#include <stdio.h>

#include "js8_activity_json.h"
typedef struct {
    FILE *file;
    Js8LogMetadata metadata;
    const char *error; /* First failure, latched until cleanup. */
} Js8ActivityLog;

void js8_log_open(Js8ActivityLog *log, const char *path, const Js8LogMetadata *metadata);
/* Event must be a successful js8_activity_build snapshot. One bounded line is
 * constructed before one fwrite; failed I/O may leave a partial last line.
 * Caller continues decoding and reports the latched error after cleanup.
 */
void js8_log_event(Js8ActivityLog *log, const Js8Activity *event);
void js8_log_flush(Js8ActivityLog *log);
void js8_log_close(Js8ActivityLog *log);
#endif

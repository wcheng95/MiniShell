#ifndef JS8_ACTIVITY_JSON_H
#define JS8_ACTIVITY_JSON_H
#include "js8_activity.h"
/* Caller-owned metadata; seconds counted from 0001-01-01T00:00:00Z. */
typedef struct {
    int have_dial, have_utc;
    int64_t dial_hz;
    uint64_t start_seconds;
} Js8LogMetadata;
typedef int (*Js8JsonWrite)(void *context, const char *bytes, size_t length);
/* Successful js8_activity_build snapshot and validated metadata required.
 * One bounded line, one caller write. Returns NULL or a stable error reason. */
const char *js8_activity_json(const Js8LogMetadata *metadata, const Js8Activity *event,
                              Js8JsonWrite write, void *context);
int js8_log_parse_dial(const char *text, int64_t *out);
int js8_log_parse_utc(const char *text, uint64_t *out);
int js8_log_format_utc(uint64_t seconds, char out[21]);
#endif

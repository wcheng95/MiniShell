#pragma once
#include <stdbool.h>
void transcript_init(void);
void transcript_append(char ch);
void transcript_text(const char *text);
void transcript_backspace(void);
void transcript_note_open(void);
void transcript_note_close(void);
void transcript_update(void);
void transcript_finalize(void);
/* Caller owns the idle/audio boundary; false discards pending records. */
void transcript_drain(bool persist);
/* One record per normal idle tick, so app_core rechecks input between records. */
void transcript_drain_one(void);

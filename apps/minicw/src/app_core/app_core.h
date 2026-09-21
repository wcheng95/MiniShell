#pragma once
#include <stdbool.h>
void app_core_init(void);
void app_core_step(void);
void app_core_shutdown(void);
void app_core_save_on_exit(void);

/* After Tone close: persist on normal shutdown, otherwise release queued RAM. */
void app_core_finish_transcript(bool persist);

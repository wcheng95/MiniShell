#pragma once

#include "shell_editor.h"
#include <stdint.h>

/* Called after the interactive input burst settles. Silent no-op
 * unless the active pathname has a longer common prefix that fits in full. */
bool shell_completion_expand(shell_editor_t *editor);

/* Reset pending on any input before decoding it; only a successful printable
 * insertion rearms it. Readers discard pending work on submission/exit. */
typedef struct {
    uint64_t deadline_us;
    bool pending;
} shell_completion_pending_t;

void shell_completion_defer(shell_completion_pending_t *state, uint64_t now_us);
int shell_completion_timeout_ms(const shell_completion_pending_t *state, uint64_t now_us);
bool shell_completion_poll(shell_completion_pending_t *state, shell_editor_t *editor,
                           uint64_t now_us);

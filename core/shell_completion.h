#pragma once

#include "shell_editor.h"

/* Called only for a resident Tab request. Silent no-op unless the active
 * pathname has a longer common prefix that fits in full. */
bool shell_completion_expand(shell_editor_t *editor);

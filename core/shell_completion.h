#pragma once

#include "shell_editor.h"
#include "minishell/api.h"

typedef void (*shell_completion_emit_fn)(const mini_fs_dir_entry_t *entry, void *ctx);
typedef enum {
    SHELL_COMPLETION_NONE, SHELL_COMPLETION_EXPANDED, SHELL_COMPLETION_LISTED
} shell_completion_result_t;

/* A Tab expands OR lists. The callback is invoked only in the output pass;
 * validation failures emit nothing. Listing preserves the complete editor. */
shell_completion_result_t shell_completion_tab(shell_editor_t *editor,
                                                shell_completion_emit_fn emit, void *ctx);
/* Expansion-only query, retaining the T078 private contract. */
bool shell_completion_expand(shell_editor_t *editor);

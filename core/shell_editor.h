#pragma once

#include <stdbool.h>
#include <stddef.h>

#define SHELL_LINE_MAX 256u
#define SHELL_HISTORY_COUNT 10u

typedef enum {
    SHELL_EDIT_CHAR, SHELL_EDIT_BACKSPACE, SHELL_EDIT_DELETE,
    SHELL_EDIT_LEFT, SHELL_EDIT_RIGHT, SHELL_EDIT_HOME, SHELL_EDIT_END,
    SHELL_EDIT_PREVIOUS, SHELL_EDIT_NEXT
} shell_edit_action_t;

typedef struct {
    char line[SHELL_LINE_MAX];
    char draft[SHELL_LINE_MAX];
    char history[SHELL_HISTORY_COUNT][SHELL_LINE_MAX];
    size_t length, cursor, draft_cursor;
    size_t first, count, navigation;
} shell_editor_t;

/* One core-owned instance per resident session. No platform or service access. */
void shell_editor_init(shell_editor_t *editor);
void shell_editor_begin(shell_editor_t *editor);
bool shell_editor_edit(shell_editor_t *editor, shell_edit_action_t action, unsigned character);
void shell_editor_remember(shell_editor_t *editor);

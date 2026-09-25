#include "shell_editor.h"

#include <string.h>

void shell_editor_begin(shell_editor_t *e)
{
    e->line[0] = e->draft[0] = '\0';
    e->length = e->cursor = e->draft_cursor = 0u;
    e->navigation = e->count;
}

void shell_editor_init(shell_editor_t *e)
{
    memset(e, 0, sizeof(*e));
    shell_editor_begin(e);
}

void shell_editor_remember(shell_editor_t *e)
{
    const char *p = e->line;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
    if (*p == '\0') return;
    size_t slot = (e->first + e->count) % SHELL_HISTORY_COUNT;
    strcpy(e->history[slot], e->line);
    if (e->count < SHELL_HISTORY_COUNT) ++e->count;
    else e->first = (e->first + 1u) % SHELL_HISTORY_COUNT;
}

bool shell_editor_edit(shell_editor_t *e, shell_edit_action_t action, unsigned character)
{
    switch (action) {
    case SHELL_EDIT_PREVIOUS:
        if (e->navigation == 0u) return false;
        if (e->navigation == e->count) {
            strcpy(e->draft, e->line);
            e->draft_cursor = e->cursor;
        }
        --e->navigation;
        break;
    case SHELL_EDIT_NEXT:
        if (e->navigation == e->count) return false;
        if (++e->navigation == e->count) {
            strcpy(e->line, e->draft);
            e->length = strlen(e->line);
            e->cursor = e->draft_cursor;
            return true;
        }
        break;
    case SHELL_EDIT_CHAR:
        if (character < 0x20u || character > 0x7eu || e->length == SHELL_LINE_MAX - 1u)
            return false;
        memmove(e->line + e->cursor + 1u, e->line + e->cursor, e->length - e->cursor + 1u);
        e->line[e->cursor++] = (char)character;
        ++e->length;
        return true;
    case SHELL_EDIT_BACKSPACE:
        if (e->cursor == 0u) return false;
        --e->cursor;
        memmove(e->line + e->cursor, e->line + e->cursor + 1u, e->length - e->cursor);
        --e->length;
        return true;
    case SHELL_EDIT_DELETE:
        if (e->cursor == e->length) return false;
        memmove(e->line + e->cursor, e->line + e->cursor + 1u, e->length - e->cursor);
        --e->length;
        return true;
    case SHELL_EDIT_LEFT:
        if (e->cursor == 0u) return false;
        --e->cursor;
        return true;
    case SHELL_EDIT_RIGHT:
        if (e->cursor == e->length) return false;
        ++e->cursor;
        return true;
    case SHELL_EDIT_HOME:
        if (e->cursor == 0u) return false;
        e->cursor = 0u;
        return true;
    case SHELL_EDIT_END:
        if (e->cursor == e->length) return false;
        e->cursor = e->length;
        return true;
    default:
        return false;
    }
    strcpy(e->line, e->history[(e->first + e->navigation) % SHELL_HISTORY_COUNT]);
    e->cursor = e->length = strlen(e->line);
    return true;
}

#include "shell_completion.h"
#include "minishell/api.h"

#include <string.h>

static bool shell_space(char ch)
{
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

static bool bare_operand(const char *command, size_t length, size_t index)
{
    static const char *const commands[] = {
        "cd", "ls", "cat", "df", "nano", "mkdir", "rm", "rmdir", "cp", "mv"
    };
    for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); ++i) {
        if (strlen(commands[i]) == length && !memcmp(command, commands[i], length))
            return index == 1u || (index == 2u &&
                   (!strcmp(commands[i], "cp") || !strcmp(commands[i], "mv")));
    }
    return false;
}

bool shell_completion_expand(shell_editor_t *e)
{
    if (!e->cursor || (e->cursor < e->length && !shell_space(e->line[e->cursor])) ||
        shell_space(e->line[e->cursor - 1u])) return false;

    size_t pos = 0u, index = 0u, command = 0u, command_length = 0u, start = 0u;
    while (pos < e->cursor) {
        while (pos < e->cursor && shell_space(e->line[pos])) ++pos;
        start = pos;
        while (pos < e->cursor && !shell_space(e->line[pos])) ++pos;
        if (index == 0u) { command = start; command_length = pos - start; }
        if (pos == e->cursor) break;
        ++index;
    }
    if (!index) return false;

    size_t component = start;
    for (size_t i = start; i < e->cursor; ++i)
        if (e->line[i] == '/') component = i + 1u;
    if (component == e->cursor) return false;
    if (component == start && !bare_operand(e->line + command, command_length, index))
        return false;

    char parent[SHELL_LINE_MAX];
    if (component == start) strcpy(parent, ".");
    else {
        size_t length = component - start - 1u;
        if (!length) length = 1u;
        memcpy(parent, e->line + start, length);
        parent[length] = '\0';
    }
    const mini_api_t *api = mini_api_get();
    const mini_fs_api_t *fs = api ? api->fs : NULL;
    if (!fs || !fs->dir_open || !fs->dir_read || !fs->dir_close) return false;
    mini_dir_t dir = MINI_DIR_INVALID;
    if (fs->dir_open(parent, &dir) != MINI_OK) return false;

    char common[MINI_FS_NAME_MAX + 1u];
    size_t common_length = 0u, prefix_length = e->cursor - component;
    bool found = false, ok = true;
    for (;;) {
        mini_fs_dir_entry_t entry = {.struct_size = sizeof(entry)};
        uint32_t has_entry = 0u;
        if (fs->dir_read(dir, &entry, &has_entry) != MINI_OK) { ok = false; break; }
        if (!has_entry) break;
        if (!strcmp(entry.name, ".") || !strcmp(entry.name, "..") ||
            (entry.name[0] == '.' && e->line[component] != '.')) continue;
        size_t length = 0u;
        while (entry.name[length] && !shell_space(entry.name[length])) ++length;
        if (entry.name[length] || length < prefix_length ||
            memcmp(entry.name, e->line + component, prefix_length)) continue;
        if (!found) {
            memcpy(common, entry.name, length);
            common_length = length;
            found = true;
        } else {
            size_t i = 0u;
            while (i < common_length && i < length && common[i] == entry.name[i]) ++i;
            common_length = i;
        }
    }
    /* Even a late read/close error cancels the entire expansion. */
    if (fs->dir_close(dir) != MINI_OK) ok = false;
    if (!ok || !found || common_length <= prefix_length) return false;
    size_t extra = common_length - prefix_length;
    if (extra > SHELL_LINE_MAX - 1u - e->length) return false;
    memmove(e->line + e->cursor + extra, e->line + e->cursor, e->length - e->cursor + 1u);
    memcpy(e->line + e->cursor, common + prefix_length, extra);
    e->cursor += extra;
    e->length += extra;
    return true;
}

void shell_completion_defer(shell_completion_pending_t *state, uint64_t now_us)
{
    state->pending = true;
    state->deadline_us = now_us + 25000u;
}

int shell_completion_timeout_ms(const shell_completion_pending_t *state, uint64_t now_us)
{
    if (!state->pending) return -1;
    if (now_us >= state->deadline_us) return 0;
    return (int)((state->deadline_us - now_us + 999u) / 1000u);
}

bool shell_completion_poll(shell_completion_pending_t *state, shell_editor_t *editor,
                           uint64_t now_us)
{
    if (shell_completion_timeout_ms(state, now_us) != 0) return false;
    state->pending = false;
    return shell_completion_expand(editor);
}

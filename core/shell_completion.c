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

typedef struct {
    char parent[SHELL_LINE_MAX];
    const char *prefix;
    size_t prefix_length;
    const mini_fs_api_t *fs;
} completion_context_t;

static bool context(const shell_editor_t *e, completion_context_t *out)
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

    char *parent = out->parent;
    if (component == start) strcpy(parent, ".");
    else {
        size_t length = component - start - 1u;
        if (!length) length = 1u;
        memcpy(parent, e->line + start, length);
        parent[length] = '\0';
    }
    out->prefix = e->line + component;
    out->prefix_length = e->cursor - component;
    const mini_api_t *api = mini_api_get();
    out->fs = api ? api->fs : NULL;
    return out->fs && out->fs->dir_open && out->fs->dir_read && out->fs->dir_close;
}

static bool matches(const completion_context_t *c, const char *name)
{
    if (!strcmp(name, ".") || !strcmp(name, "..") ||
        (name[0] == '.' && c->prefix[0] != '.')) return false;
    size_t length = 0u;
    while (name[length] && !shell_space(name[length])) ++length;
    return !name[length] && length >= c->prefix_length &&
           !memcmp(name, c->prefix, c->prefix_length);
}

/* Both passes use identical public-FS traversal and filtering. Count saturates
 * at two: output has no candidate cap and needs no count-sized storage. */
static bool walk(const completion_context_t *c, shell_completion_emit_fn emit,
                 void *ctx, unsigned *count)
{
    *count = 0u;
    mini_dir_t dir = MINI_DIR_INVALID;
    if (c->fs->dir_open(c->parent, &dir) != MINI_OK) return false;
    bool ok = true;
    for (;;) {
        mini_fs_dir_entry_t entry = {.struct_size = sizeof(entry)};
        uint32_t has_entry = 0u;
        if (c->fs->dir_read(dir, &entry, &has_entry) != MINI_OK) { ok = false; break; }
        if (!has_entry) break;
        if (!matches(c, entry.name)) continue;
        emit(&entry, ctx);
        if (*count < 2u) ++*count;
    }
    if (c->fs->dir_close(dir) != MINI_OK) ok = false;
    return ok;
}

typedef struct {
    char common[MINI_FS_NAME_MAX + 1u];
    size_t length;
    bool found;
} common_prefix_t;

static void common_prefix(const mini_fs_dir_entry_t *entry, void *ctx)
{
    common_prefix_t *p = (common_prefix_t *)ctx;
    if (!p->found) {
        p->length = strlen(entry->name);
        memcpy(p->common, entry->name, p->length);
        p->found = true;
    } else {
        size_t i = 0u;
        while (i < p->length && p->common[i] == entry->name[i]) ++i;
        p->length = i;
    }
}

shell_completion_result_t shell_completion_tab(shell_editor_t *e,
                                                shell_completion_emit_fn emit, void *ctx)
{
    completion_context_t c;
    if (!context(e, &c)) return SHELL_COMPLETION_NONE;
    common_prefix_t p = {0};
    unsigned count;
    if (!walk(&c, common_prefix, &p, &count)) return SHELL_COMPLETION_NONE;
    if (p.found && p.length > c.prefix_length) {
        size_t extra = p.length - c.prefix_length;
        if (extra <= SHELL_LINE_MAX - 1u - e->length) {
            memmove(e->line + e->cursor + extra, e->line + e->cursor, e->length - e->cursor + 1u);
            memcpy(e->line + e->cursor, p.common + c.prefix_length, extra);
            e->cursor += extra;
            e->length += extra;
            return SHELL_COMPLETION_EXPANDED;
        }
    }
    if (count < 2u || !emit) return SHELL_COMPLETION_NONE;
    /* Validation succeeded. Later output errors may leave printed choices;
     * callers restore the prompt iff at least one choice was emitted. */
    (void)walk(&c, emit, ctx, &count);
    return count ? SHELL_COMPLETION_LISTED : SHELL_COMPLETION_NONE;
}

bool shell_completion_expand(shell_editor_t *e)
{
    return shell_completion_tab(e, NULL, NULL) == SHELL_COMPLETION_EXPANDED;
}

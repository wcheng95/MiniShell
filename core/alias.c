#include "alias.h"
#include <string.h>

static bool space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

const char *minishell_alias_match(char *line, const char *name, size_t name_length)
{
    size_t length = strlen(line);
    if (length && line[length - 1] == '\n') line[--length] = '\0';
    if (length && line[length - 1] == '\r') line[--length] = '\0';
    const char *first = line;
    while (space(*first)) ++first;
    if (!*first || *first == '#') return NULL;
    const char *separator = strchr(line, '=');
    if (!separator || separator == line || !separator[1]) return NULL;
    for (const char *p = line; p != separator; ++p)
        if (space(*p)) return NULL;
    size_t alias_length = (size_t)(separator - line);
    return alias_length == name_length && memcmp(line, name, name_length) == 0
        ? separator + 1 : NULL;
}

bool minishell_alias_expand(const char *replacement, const char *arguments,
                            char *out, size_t capacity)
{
    size_t replacement_length = strlen(replacement), argument_length = strlen(arguments);
    if (replacement_length >= capacity) return false;
    size_t remaining = capacity - replacement_length - 1;
    if (argument_length && (remaining == 0 || argument_length > remaining - 1)) return false;
    memcpy(out, replacement, replacement_length);
    size_t used = replacement_length;
    if (argument_length) {
        out[used++] = ' ';
        memcpy(out + used, arguments, argument_length);
        used += argument_length;
    }
    out[used] = '\0';
    return true;
}

#pragma once

#include <string.h>

#include "minishell/api.h"

/* App-private bound, including the terminator; no public path API dependency. */
#define FILE_DESTINATION_CAP 512u

/* Caller must first stat source and require a regular file. Non-directory
 * operands remain literal. Only the Filesystem service resolves CWD. */
static inline mini_result_t file_destination_resolve(
    const mini_fs_api_t *fs, const char *source, const char *destination,
    char joined[FILE_DESTINATION_CAP], const char **effective)
{
    if (fs == NULL || fs->stat == NULL || source == NULL || *source == '\0' ||
        destination == NULL || *destination == '\0' || joined == NULL ||
        effective == NULL) return MINI_ERR_INVALID;

    mini_fs_stat_t st = {.struct_size = sizeof(st)};
    mini_result_t result = fs->stat(destination, &st);
    if (result == MINI_ERR_NOT_FOUND ||
        (result == MINI_OK && st.type == MINI_FS_TYPE_FILE)) {
        *effective = destination;
        return MINI_OK;
    }
    if (result != MINI_OK) return result;
    if (st.type != MINI_FS_TYPE_DIRECTORY) return MINI_ERR_INVALID;

    /* Scan backwards for the last surviving filename component. Dot components
     * and separators do not count; each .. cancels one preceding component. */
    size_t end = strlen(source);
    size_t start = end;
    size_t parents = 0u;
    size_t name_len = 0u;
    while (end != 0u) {
        if (source[end - 1u] == '/') { --end; continue; }
        start = end;
        while (start != 0u && source[start - 1u] != '/') --start;
        size_t length = end - start;
        if (length == 1u && source[start] == '.') {
            /* Ignored by MiniShell path normalization. */
        } else if (length == 2u && source[start] == '.' && source[start + 1u] == '.') {
            ++parents;
        } else if (parents != 0u) {
            --parents;
        } else {
            name_len = length;
            break;
        }
        end = start;
    }
    if (name_len == 0u) return MINI_ERR_INVALID;

    size_t directory_len = strlen(destination);
    size_t separator = destination[directory_len - 1u] == '/' ? 0u : 1u;
    if (directory_len >= FILE_DESTINATION_CAP ||
        name_len >= FILE_DESTINATION_CAP - directory_len - separator) {
        return MINI_ERR_NAME_TOO_LONG;
    }
    memcpy(joined, destination, directory_len);
    if (separator != 0u) joined[directory_len++] = '/';
    memcpy(joined + directory_len, source + start, name_len);
    joined[directory_len + name_len] = '\0';
    *effective = joined;
    return MINI_OK;
}

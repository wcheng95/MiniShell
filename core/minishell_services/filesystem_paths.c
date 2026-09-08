#include <string.h>

#include "filesystem_internal.h"

uint64_t filesystem_path_hash(const char *text)
{
    uint64_t hash = 1469598103934665603ull;
    while (*text != '\0') {
        hash ^= (uint8_t)*text++;
        hash *= 1099511628211ull;
    }
    return hash;
}

mini_result_t filesystem_path_normalize(const char *path, char *out)
{
    if (path == NULL || out == NULL || path[0] != '/') return MINI_ERR_INVALID;

    uint32_t out_len = 1u;
    out[0] = '/';
    out[1] = '\0';

    const char *p = path;
    while (*p == '/') ++p;
    while (*p != '\0') {
        const char *start = p;
        while (*p != '\0' && *p != '/') ++p;
        uint32_t len = (uint32_t)(p - start);

        if (len == 1u && start[0] == '.') {
            /* Ignore current-directory components. */
        } else if (len == 2u && start[0] == '.' && start[1] == '.') {
            if (out_len == 1u) return MINI_ERR_INVALID;
            if (out_len > 1u && out[out_len - 1u] == '/') --out_len;
            while (out_len > 1u && out[out_len - 1u] != '/') --out_len;
            if (out_len > 1u && out[out_len - 1u] == '/') --out_len;
            if (out_len == 0u) out_len = 1u;
            out[out_len] = '\0';
        } else if (len > 0u) {
            uint32_t need = out_len + (out_len > 1u ? 1u : 0u) + len + 1u;
            if (need > MINI_FS_NORMALIZED_PATH_MAX) return MINI_ERR_NAME_TOO_LONG;
            if (out_len > 1u) out[out_len++] = '/';
            memcpy(&out[out_len], start, len);
            out_len += len;
            out[out_len] = '\0';
        }

        while (*p == '/') ++p;
    }
    return MINI_OK;
}

mini_result_t filesystem_path_join_child(const char *parent,
                                         const char *name,
                                         char *out)
{
    size_t parent_len = strlen(parent);
    size_t name_len = strlen(name);
    bool root = parent_len == 1u && parent[0] == '/';
    size_t total = parent_len + (root ? 0u : 1u) + name_len + 1u;
    if (total > MINI_FS_NORMALIZED_PATH_MAX) return MINI_ERR_NAME_TOO_LONG;

    memcpy(out, parent, parent_len);
    size_t pos = parent_len;
    if (!root) out[pos++] = '/';
    memcpy(out + pos, name, name_len + 1u);
    return MINI_OK;
}

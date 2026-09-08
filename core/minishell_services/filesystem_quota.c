#include <string.h>

#include "filesystem_internal.h"

#define MINI_FS_SCAN_MAX_DEPTH 32u

static bool s_usage_valid;
static uint64_t s_used;

static mini_result_t scan_usage_path(const char *path, uint32_t depth,
                                     uint64_t *inout_used)
{
    const minishell_services_port_t *port = minishell_services_port();
    if (depth > MINI_FS_SCAN_MAX_DEPTH) return MINI_ERR_IO;

    uint32_t type = 0u;
    uint64_t size = 0u;
    mini_result_t result = port->fs_stat(port->ctx, path, &type, &size);
    if (result != MINI_OK) return result;

    if (type == MINI_FS_TYPE_FILE) {
        if (UINT64_MAX - *inout_used < size) return MINI_ERR_NO_SPACE;
        *inout_used += size;
        return MINI_OK;
    }
    if (type != MINI_FS_TYPE_DIRECTORY) return MINI_OK;
    if (port->fs_dir_open == NULL || port->fs_dir_read == NULL ||
        port->fs_dir_close == NULL) {
        return MINI_ERR_UNSUPPORTED;
    }

    minishell_backend_dir_t dir = MINISHELL_BACKEND_DIR_INVALID;
    result = port->fs_dir_open(port->ctx, path, &dir);
    if (result != MINI_OK) return result;

    for (;;) {
        char name[MINI_FS_NAME_MAX + 1u] = {0};
        uint32_t child_type = 0u;
        uint32_t has_entry = 0u;
        result = port->fs_dir_read(port->ctx, dir, name, (uint32_t)sizeof(name),
                                   &child_type, &has_entry);
        if (result != MINI_OK || has_entry == 0u) break;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;

        char child[MINI_FS_NORMALIZED_PATH_MAX];
        result = filesystem_path_join_child(path, name, child);
        if (result != MINI_OK) break;
        result = scan_usage_path(child, depth + 1u, inout_used);
        if (result != MINI_OK) break;
    }

    mini_result_t close_result = port->fs_dir_close(port->ctx, dir);
    if (result == MINI_OK && close_result != MINI_OK) result = close_result;
    return result;
}

void filesystem_quota_reset(void)
{
    s_usage_valid = false;
    s_used = 0u;
}

mini_result_t filesystem_quota_refresh(void)
{
    if (minishell_storage_limit_bytes() == 0u) {
        filesystem_quota_reset();
        return MINI_OK;
    }

    uint64_t used = 0u;
    mini_result_t result = scan_usage_path("/", 0u, &used);
    if (result != MINI_OK) {
        s_usage_valid = false;
        return result;
    }

    s_used = used;
    s_usage_valid = true;
    return MINI_OK;
}

bool filesystem_quota_valid(void)
{
    return s_usage_valid;
}

uint64_t filesystem_quota_used(void)
{
    return s_used;
}

uint64_t filesystem_quota_free(uint64_t limit)
{
    return s_used < limit ? limit - s_used : 0u;
}

void filesystem_quota_add(uint64_t bytes)
{
    if (!s_usage_valid) return;
    s_used = UINT64_MAX - s_used < bytes ? UINT64_MAX : s_used + bytes;
}

void filesystem_quota_subtract(uint64_t bytes)
{
    if (!s_usage_valid) return;
    s_used = bytes <= s_used ? s_used - bytes : 0u;
}

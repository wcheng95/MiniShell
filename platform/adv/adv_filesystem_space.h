#pragma once
#include <stdbool.h>
#include <string.h>
#include "minishell/api.h"

static inline bool adv_fs_volume_path(const char *path, const char *root, bool mounted)
{
    if (!mounted || path == NULL) return false;
    if (strcmp(path, root) == 0) return true;
    size_t length = strlen(root);
    return strncmp(path, root, length) == 0 && path[length] == '/';
}

typedef mini_result_t (*adv_volume_info_fn)(const char *, uint64_t *, uint64_t *);

/* ESP-IDF's FAT info takes a mount root, not an arbitrary descendant. */
static inline mini_result_t adv_fs_volume_space(const char *path, bool flash, bool sd,
                                                adv_volume_info_fn info,
                                                uint64_t *total, uint64_t *free_bytes)
{
    if (adv_fs_volume_path(path, "/flash", flash)) return info("/flash", total, free_bytes);
    if (adv_fs_volume_path(path, "/sd", sd)) return info("/sd", total, free_bytes);
    return MINI_ERR_NOT_FOUND;
}

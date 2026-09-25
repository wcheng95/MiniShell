#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "services_internal.h"

#define MINI_FS_MAX_OPEN_FILES 32u
#define MINI_FS_MAX_OPEN_DIRS 16u
#define MINI_FS_NORMALIZED_PATH_MAX MINISHELL_FILESYSTEM_PATH_CAP

typedef struct {
    minishell_backend_file_t backend;
    mini_file_t public_handle;
    uint32_t flags;
    uint64_t logical_size;
    uint64_t position;
    uint64_t path_hash;
} filesystem_file_slot_t;

typedef struct {
    minishell_backend_dir_t backend;
    mini_dir_t public_handle;
} filesystem_dir_slot_t;

/* Path helpers: pure MiniShell namespace mechanics. */
uint64_t filesystem_path_hash(const char *text);
mini_result_t filesystem_path_normalize(const char *path, char *out);
/* cwd must already be canonical; outputs are always absolute. */
mini_result_t filesystem_path_resolve(const char *cwd, const char *path, char *out);
mini_result_t filesystem_path_join_child(const char *parent,
                                         const char *name,
                                         char *out);

/* Handle registry: private logical-handle identity and lifetime bookkeeping. */
void filesystem_handles_reset(void);
uint32_t filesystem_handles_find_free_file(void);
uint32_t filesystem_handles_find_free_dir(void);
filesystem_file_slot_t *filesystem_handles_lookup_file(mini_file_t file);
filesystem_dir_slot_t *filesystem_handles_lookup_dir(mini_dir_t dir);
bool filesystem_handles_path_hash_in_use(uint64_t path_hash);
bool filesystem_handles_writable_hash_in_use(uint64_t path_hash);
mini_file_t filesystem_handles_activate_file(uint32_t index,
                                             minishell_backend_file_t backend,
                                             uint32_t flags,
                                             uint64_t logical_size,
                                             uint64_t position,
                                             uint64_t path_hash);
mini_dir_t filesystem_handles_activate_dir(uint32_t index,
                                           minishell_backend_dir_t backend);
minishell_backend_file_t filesystem_handles_release_file(filesystem_file_slot_t *slot);
minishell_backend_dir_t filesystem_handles_release_dir(filesystem_dir_slot_t *slot);
void filesystem_handles_close_all(const minishell_services_port_t *port);

/* Quota/usage state: private accounting derived from the backend namespace. */
void filesystem_quota_reset(void);
mini_result_t filesystem_quota_refresh(void);
bool filesystem_quota_valid(void);
uint64_t filesystem_quota_used(void);
uint64_t filesystem_quota_free(uint64_t limit);
void filesystem_quota_add(uint64_t bytes);
void filesystem_quota_subtract(uint64_t bytes);

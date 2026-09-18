#include <string.h>

#include "filesystem_internal.h"

static filesystem_file_slot_t s_files[MINI_FS_MAX_OPEN_FILES];
static filesystem_dir_slot_t s_dirs[MINI_FS_MAX_OPEN_DIRS];
/* Shared acquisition sequence, never reset at app/port boundaries. Zero marks
 * exhaustion: fail closed instead of granting an old token a new lifetime. */
static uint32_t s_next_handle = 1u;

static uint32_t allocate_handle(void)
{
    if (s_next_handle == 0u) return 0u;
    return s_next_handle++;
}

void filesystem_handles_reset(void)
{
    memset(s_files, 0, sizeof(s_files));
    memset(s_dirs, 0, sizeof(s_dirs));
}

uint32_t filesystem_handles_find_free_file(void)
{
    if (s_next_handle == 0u) return MINI_FS_MAX_OPEN_FILES;
    for (uint32_t i = 0u; i < MINI_FS_MAX_OPEN_FILES; ++i) {
        if (s_files[i].backend == MINISHELL_BACKEND_FILE_INVALID) return i;
    }
    return MINI_FS_MAX_OPEN_FILES;
}

uint32_t filesystem_handles_find_free_dir(void)
{
    if (s_next_handle == 0u) return MINI_FS_MAX_OPEN_DIRS;
    for (uint32_t i = 0u; i < MINI_FS_MAX_OPEN_DIRS; ++i) {
        if (s_dirs[i].backend == MINISHELL_BACKEND_DIR_INVALID) return i;
    }
    return MINI_FS_MAX_OPEN_DIRS;
}

filesystem_file_slot_t *filesystem_handles_lookup_file(mini_file_t file)
{
    if (file == MINI_FILE_INVALID) return NULL;
    for (uint32_t i = 0u; i < MINI_FS_MAX_OPEN_FILES; ++i) {
        if (s_files[i].backend != MINISHELL_BACKEND_FILE_INVALID &&
            s_files[i].public_handle == file) {
            return &s_files[i];
        }
    }
    return NULL;
}

filesystem_dir_slot_t *filesystem_handles_lookup_dir(mini_dir_t dir)
{
    if (dir == MINI_DIR_INVALID) return NULL;
    for (uint32_t i = 0u; i < MINI_FS_MAX_OPEN_DIRS; ++i) {
        if (s_dirs[i].backend != MINISHELL_BACKEND_DIR_INVALID &&
            s_dirs[i].public_handle == dir) {
            return &s_dirs[i];
        }
    }
    return NULL;
}

bool filesystem_handles_writable_hash_in_use(uint64_t path_hash)
{
    for (uint32_t i = 0u; i < MINI_FS_MAX_OPEN_FILES; ++i) {
        if (s_files[i].backend != MINISHELL_BACKEND_FILE_INVALID &&
            (s_files[i].flags & MINI_FS_WRITE) != 0u &&
            s_files[i].path_hash == path_hash) {
            return true;
        }
    }
    return false;
}

mini_file_t filesystem_handles_activate_file(uint32_t index,
                                             minishell_backend_file_t backend,
                                             uint32_t flags,
                                             uint64_t logical_size,
                                             uint64_t position,
                                             uint64_t path_hash)
{
    if (index >= MINI_FS_MAX_OPEN_FILES || backend == MINISHELL_BACKEND_FILE_INVALID) {
        return MINI_FILE_INVALID;
    }

    filesystem_file_slot_t *slot = &s_files[index];
    if (slot->backend != MINISHELL_BACKEND_FILE_INVALID) return MINI_FILE_INVALID;
    mini_file_t handle = allocate_handle();
    if (handle == MINI_FILE_INVALID) return MINI_FILE_INVALID;
    slot->backend = backend;
    slot->public_handle = handle;
    slot->flags = flags;
    slot->logical_size = logical_size;
    slot->position = position;
    slot->path_hash = path_hash;
    return handle;
}

mini_dir_t filesystem_handles_activate_dir(uint32_t index,
                                           minishell_backend_dir_t backend)
{
    if (index >= MINI_FS_MAX_OPEN_DIRS || backend == MINISHELL_BACKEND_DIR_INVALID) {
        return MINI_DIR_INVALID;
    }

    filesystem_dir_slot_t *slot = &s_dirs[index];
    if (slot->backend != MINISHELL_BACKEND_DIR_INVALID) return MINI_DIR_INVALID;
    mini_dir_t handle = allocate_handle();
    if (handle == MINI_DIR_INVALID) return MINI_DIR_INVALID;
    slot->backend = backend;
    slot->public_handle = handle;
    return handle;
}

minishell_backend_file_t filesystem_handles_release_file(filesystem_file_slot_t *slot)
{
    if (slot == NULL) return MINISHELL_BACKEND_FILE_INVALID;
    minishell_backend_file_t backend = slot->backend;
    memset(slot, 0, sizeof(*slot));
    return backend;
}

minishell_backend_dir_t filesystem_handles_release_dir(filesystem_dir_slot_t *slot)
{
    if (slot == NULL) return MINISHELL_BACKEND_DIR_INVALID;
    minishell_backend_dir_t backend = slot->backend;
    memset(slot, 0, sizeof(*slot));
    return backend;
}

void filesystem_handles_close_all(const minishell_services_port_t *port)
{
    if (port == NULL) return;

    for (uint32_t i = 0u; i < MINI_FS_MAX_OPEN_FILES; ++i) {
        if (s_files[i].backend != MINISHELL_BACKEND_FILE_INVALID) {
            if (port->fs_close != NULL) {
                (void)port->fs_close(port->ctx, s_files[i].backend);
            }
            memset(&s_files[i], 0, sizeof(s_files[i]));
        }
    }

    for (uint32_t i = 0u; i < MINI_FS_MAX_OPEN_DIRS; ++i) {
        if (s_dirs[i].backend != MINISHELL_BACKEND_DIR_INVALID) {
            if (port->fs_dir_close != NULL) {
                (void)port->fs_dir_close(port->ctx, s_dirs[i].backend);
            }
            memset(&s_dirs[i], 0, sizeof(s_dirs[i]));
        }
    }
}

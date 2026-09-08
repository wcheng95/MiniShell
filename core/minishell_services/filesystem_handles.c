#include <string.h>

#include "filesystem_internal.h"

static filesystem_file_slot_t s_files[MINI_FS_MAX_OPEN_FILES];
static filesystem_dir_slot_t s_dirs[MINI_FS_MAX_OPEN_DIRS];
static uint16_t s_generation = 1u;

static mini_file_t make_file_handle(uint32_t index, uint16_t generation)
{
    return ((uint32_t)generation << 16) | (index + 1u);
}

static mini_dir_t make_dir_handle(uint32_t index, uint16_t generation)
{
    return ((uint32_t)generation << 16) | (index + 1u);
}

void filesystem_handles_reset(void)
{
    memset(s_files, 0, sizeof(s_files));
    memset(s_dirs, 0, sizeof(s_dirs));
}

void filesystem_handles_advance_generation(void)
{
    ++s_generation;
    if (s_generation == 0u) s_generation = 1u;
}

uint32_t filesystem_handles_find_free_file(void)
{
    for (uint32_t i = 0u; i < MINI_FS_MAX_OPEN_FILES; ++i) {
        if (s_files[i].backend == MINISHELL_BACKEND_FILE_INVALID) return i;
    }
    return MINI_FS_MAX_OPEN_FILES;
}

uint32_t filesystem_handles_find_free_dir(void)
{
    for (uint32_t i = 0u; i < MINI_FS_MAX_OPEN_DIRS; ++i) {
        if (s_dirs[i].backend == MINISHELL_BACKEND_DIR_INVALID) return i;
    }
    return MINI_FS_MAX_OPEN_DIRS;
}

filesystem_file_slot_t *filesystem_handles_lookup_file(mini_file_t file)
{
    if (file == MINI_FILE_INVALID) return NULL;
    uint32_t raw_slot = file & 0xFFFFu;
    uint16_t generation = (uint16_t)(file >> 16);
    if (raw_slot == 0u || raw_slot > MINI_FS_MAX_OPEN_FILES) return NULL;

    filesystem_file_slot_t *slot = &s_files[raw_slot - 1u];
    if (slot->backend == MINISHELL_BACKEND_FILE_INVALID ||
        slot->generation != generation) {
        return NULL;
    }
    return slot;
}

filesystem_dir_slot_t *filesystem_handles_lookup_dir(mini_dir_t dir)
{
    if (dir == MINI_DIR_INVALID) return NULL;
    uint32_t raw_slot = dir & 0xFFFFu;
    uint16_t generation = (uint16_t)(dir >> 16);
    if (raw_slot == 0u || raw_slot > MINI_FS_MAX_OPEN_DIRS) return NULL;

    filesystem_dir_slot_t *slot = &s_dirs[raw_slot - 1u];
    if (slot->backend == MINISHELL_BACKEND_DIR_INVALID ||
        slot->generation != generation) {
        return NULL;
    }
    return slot;
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
    slot->backend = backend;
    slot->generation = s_generation;
    slot->flags = flags;
    slot->logical_size = logical_size;
    slot->position = position;
    slot->path_hash = path_hash;
    return make_file_handle(index, s_generation);
}

mini_dir_t filesystem_handles_activate_dir(uint32_t index,
                                           minishell_backend_dir_t backend)
{
    if (index >= MINI_FS_MAX_OPEN_DIRS || backend == MINISHELL_BACKEND_DIR_INVALID) {
        return MINI_DIR_INVALID;
    }

    filesystem_dir_slot_t *slot = &s_dirs[index];
    slot->backend = backend;
    slot->generation = s_generation;
    return make_dir_handle(index, s_generation);
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

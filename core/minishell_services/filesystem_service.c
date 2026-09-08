#include <string.h>

#include "filesystem_internal.h"

#define MINI_FS_KNOWN_FLAGS (MINI_FS_READ | MINI_FS_WRITE | MINI_FS_CREATE | \
                             MINI_FS_EXCL | MINI_FS_TRUNC | MINI_FS_APPEND)

static bool s_available;

static mini_result_t validate_flags(uint32_t flags)
{
    if ((flags & ~MINI_FS_KNOWN_FLAGS) != 0u) return MINI_ERR_INVALID;
    if ((flags & (MINI_FS_READ | MINI_FS_WRITE)) == 0u) return MINI_ERR_INVALID;
    if ((flags & MINI_FS_CREATE) != 0u && (flags & MINI_FS_WRITE) == 0u) return MINI_ERR_INVALID;
    if ((flags & MINI_FS_EXCL) != 0u && (flags & MINI_FS_CREATE) == 0u) return MINI_ERR_INVALID;
    if ((flags & MINI_FS_TRUNC) != 0u && (flags & MINI_FS_WRITE) == 0u) return MINI_ERR_INVALID;
    if ((flags & MINI_FS_APPEND) != 0u && (flags & MINI_FS_WRITE) == 0u) return MINI_ERR_INVALID;
    return MINI_OK;
}

static mini_result_t fs_open(const char *path, uint32_t flags, mini_file_t *out_file)
{
    const minishell_services_port_t *port = minishell_services_port();
    if (out_file == NULL) return MINI_ERR_INVALID;
    *out_file = MINI_FILE_INVALID;
    if (!s_available) return MINI_ERR_UNSUPPORTED;

    mini_result_t result = validate_flags(flags);
    if (result != MINI_OK) return result;

    char normalized[MINI_FS_NORMALIZED_PATH_MAX];
    result = filesystem_path_normalize(path, normalized);
    if (result != MINI_OK) return result;

    uint64_t path_hash = filesystem_path_hash(normalized);
    if ((flags & MINI_FS_WRITE) != 0u &&
        filesystem_handles_writable_hash_in_use(path_hash)) {
        return MINI_ERR_ACCESS;
    }

    uint32_t old_type = 0u;
    uint64_t old_size = 0u;
    bool old_exists = false;
    result = port->fs_stat(port->ctx, normalized, &old_type, &old_size);
    if (result == MINI_OK) {
        old_exists = true;
    } else if (result != MINI_ERR_NOT_FOUND) {
        return result;
    }

    uint32_t index = filesystem_handles_find_free_file();
    if (index == MINI_FS_MAX_OPEN_FILES) return MINI_ERR_TOO_MANY_OPEN;

    if (minishell_storage_limit_bytes() != 0u &&
        filesystem_quota_refresh() != MINI_OK) {
        return MINI_ERR_IO;
    }

    minishell_backend_file_t backend = MINISHELL_BACKEND_FILE_INVALID;
    result = port->fs_open(port->ctx, normalized, flags, &backend);
    if (result != MINI_OK) return result;
    if (backend == MINISHELL_BACKEND_FILE_INVALID) return MINI_ERR_IO;

    uint64_t logical_size = old_exists && old_type == MINI_FS_TYPE_FILE ? old_size : 0u;
    if ((flags & MINI_FS_TRUNC) != 0u && old_exists && old_type == MINI_FS_TYPE_FILE) {
        filesystem_quota_subtract(old_size);
        logical_size = 0u;
    }

    uint64_t position = (flags & MINI_FS_APPEND) != 0u ? logical_size : 0u;
    *out_file = filesystem_handles_activate_file(index, backend, flags,
                                                 logical_size, position, path_hash);
    if (*out_file == MINI_FILE_INVALID) {
        (void)port->fs_close(port->ctx, backend);
        return MINI_ERR_IO;
    }
    return MINI_OK;
}

static mini_result_t fs_close(mini_file_t file)
{
    const minishell_services_port_t *port = minishell_services_port();
    filesystem_file_slot_t *slot = filesystem_handles_lookup_file(file);
    if (!s_available) return MINI_ERR_UNSUPPORTED;
    if (slot == NULL) return MINI_ERR_BAD_HANDLE;

    minishell_backend_file_t backend = filesystem_handles_release_file(slot);
    return port->fs_close(port->ctx, backend);
}

static mini_result_t fs_read(mini_file_t file, void *buffer,
                             uint32_t size, uint32_t *out_read)
{
    const minishell_services_port_t *port = minishell_services_port();
    if (out_read == NULL) return MINI_ERR_INVALID;
    *out_read = 0u;
    if (!s_available) return MINI_ERR_UNSUPPORTED;
    if (size > 0u && buffer == NULL) return MINI_ERR_INVALID;

    filesystem_file_slot_t *slot = filesystem_handles_lookup_file(file);
    if (slot == NULL) return MINI_ERR_BAD_HANDLE;
    if ((slot->flags & MINI_FS_READ) == 0u) return MINI_ERR_ACCESS;
    if (size == 0u) return MINI_OK;

    mini_result_t result = port->fs_read(port->ctx, slot->backend,
                                         buffer, size, out_read);
    if (result == MINI_OK) {
        if (UINT64_MAX - slot->position < *out_read) return MINI_ERR_IO;
        slot->position += *out_read;
    }
    return result;
}

static mini_result_t fs_write(mini_file_t file, const void *buffer,
                              uint32_t size, uint32_t *out_written)
{
    const minishell_services_port_t *port = minishell_services_port();
    if (out_written == NULL) return MINI_ERR_INVALID;
    *out_written = 0u;
    if (!s_available) return MINI_ERR_UNSUPPORTED;
    if (size > 0u && buffer == NULL) return MINI_ERR_INVALID;

    filesystem_file_slot_t *slot = filesystem_handles_lookup_file(file);
    if (slot == NULL) return MINI_ERR_BAD_HANDLE;
    if ((slot->flags & MINI_FS_WRITE) == 0u) return MINI_ERR_ACCESS;
    if (size == 0u) return MINI_OK;

    uint64_t start = (slot->flags & MINI_FS_APPEND) != 0u
                         ? slot->logical_size
                         : slot->position;
    if ((uint64_t)size > UINT64_MAX - start) return MINI_ERR_NO_SPACE;

    uint64_t requested_end = start + size;
    uint64_t requested_growth = requested_end > slot->logical_size
                                    ? requested_end - slot->logical_size
                                    : 0u;

    uint64_t limit = minishell_storage_limit_bytes();
    if (limit != 0u) {
        if (filesystem_quota_refresh() != MINI_OK) return MINI_ERR_IO;
        if (requested_growth > filesystem_quota_free(limit)) return MINI_ERR_NO_SPACE;
    }

    mini_result_t result = port->fs_write(port->ctx, slot->backend,
                                          buffer, size, out_written);
    if (result != MINI_OK) return result;
    if (*out_written == 0u || *out_written > size) return MINI_ERR_IO;

    uint64_t actual_end = start + *out_written;
    uint64_t actual_growth = actual_end > slot->logical_size
                                 ? actual_end - slot->logical_size
                                 : 0u;
    filesystem_quota_add(actual_growth);
    if (actual_end > slot->logical_size) slot->logical_size = actual_end;
    slot->position = actual_end;
    return MINI_OK;
}

static mini_result_t fs_seek(mini_file_t file, int64_t offset,
                             uint32_t origin, uint64_t *out_position)
{
    const minishell_services_port_t *port = minishell_services_port();
    if (out_position == NULL) return MINI_ERR_INVALID;
    *out_position = 0u;
    if (!s_available) return MINI_ERR_UNSUPPORTED;
    if (origin != MINI_FS_SEEK_SET && origin != MINI_FS_SEEK_CUR &&
        origin != MINI_FS_SEEK_END) {
        return MINI_ERR_INVALID;
    }

    filesystem_file_slot_t *slot = filesystem_handles_lookup_file(file);
    if (slot == NULL) return MINI_ERR_BAD_HANDLE;

    mini_result_t result = port->fs_seek(port->ctx, slot->backend,
                                         offset, origin, out_position);
    if (result == MINI_OK) slot->position = *out_position;
    return result;
}

static mini_result_t fs_sync(mini_file_t file)
{
    const minishell_services_port_t *port = minishell_services_port();
    if (!s_available) return MINI_ERR_UNSUPPORTED;

    filesystem_file_slot_t *slot = filesystem_handles_lookup_file(file);
    if (slot == NULL) return MINI_ERR_BAD_HANDLE;
    return port->fs_sync(port->ctx, slot->backend);
}

static mini_result_t fs_stat(const char *path, mini_fs_stat_t *out_stat)
{
    const minishell_services_port_t *port = minishell_services_port();
    const uint32_t v0_size = MINI_FIELD_END(mini_fs_stat_t, size);
    if (!s_available) return MINI_ERR_UNSUPPORTED;
    if (path == NULL || out_stat == NULL || out_stat->struct_size < v0_size) {
        return MINI_ERR_INVALID;
    }

    char normalized[MINI_FS_NORMALIZED_PATH_MAX];
    mini_result_t result = filesystem_path_normalize(path, normalized);
    if (result != MINI_OK) return result;

    uint32_t type = 0u;
    uint64_t size = 0u;
    result = port->fs_stat(port->ctx, normalized, &type, &size);
    if (result != MINI_OK) return result;
    out_stat->type = type;
    out_stat->size = size;
    return MINI_OK;
}

static mini_result_t fs_rename(const char *old_path, const char *new_path)
{
    const minishell_services_port_t *port = minishell_services_port();
    if (!s_available) return MINI_ERR_UNSUPPORTED;
    if (port->fs_rename == NULL) return MINI_ERR_UNSUPPORTED;

    char old_normalized[MINI_FS_NORMALIZED_PATH_MAX];
    char new_normalized[MINI_FS_NORMALIZED_PATH_MAX];
    mini_result_t result = filesystem_path_normalize(old_path, old_normalized);
    if (result != MINI_OK) return result;
    result = filesystem_path_normalize(new_path, new_normalized);
    if (result != MINI_OK) return result;

    if (strcmp(old_normalized, "/") == 0 || strcmp(new_normalized, "/") == 0) {
        return MINI_ERR_ACCESS;
    }

    uint32_t source_type = 0u;
    uint64_t source_size = 0u;
    result = port->fs_stat(port->ctx, old_normalized, &source_type, &source_size);
    if (result != MINI_OK) return result;
    if (source_type != MINI_FS_TYPE_FILE) return MINI_ERR_IS_DIR;
    if (strcmp(old_normalized, new_normalized) == 0) return MINI_OK;

    uint64_t old_hash = filesystem_path_hash(old_normalized);
    uint64_t new_hash = filesystem_path_hash(new_normalized);
    if (filesystem_handles_writable_hash_in_use(old_hash) ||
        filesystem_handles_writable_hash_in_use(new_hash)) {
        return MINI_ERR_ACCESS;
    }

    bool destination_exists = false;
    uint32_t destination_type = 0u;
    uint64_t destination_size = 0u;
    result = port->fs_stat(port->ctx, new_normalized,
                           &destination_type, &destination_size);
    if (result == MINI_OK) {
        if (destination_type != MINI_FS_TYPE_FILE) return MINI_ERR_IS_DIR;
        destination_exists = true;
    } else if (result != MINI_ERR_NOT_FOUND) {
        return result;
    }

    result = port->fs_rename(port->ctx, old_normalized, new_normalized);
    if (result != MINI_OK) return result;

    if (destination_exists) filesystem_quota_subtract(destination_size);
    return MINI_OK;
}

static mini_result_t fs_remove_file(const char *path)
{
    const minishell_services_port_t *port = minishell_services_port();
    if (!s_available) return MINI_ERR_UNSUPPORTED;
    if (port->fs_remove_file == NULL) return MINI_ERR_UNSUPPORTED;

    char normalized[MINI_FS_NORMALIZED_PATH_MAX];
    mini_result_t result = filesystem_path_normalize(path, normalized);
    if (result != MINI_OK) return result;
    if (strcmp(normalized, "/") == 0) return MINI_ERR_IS_DIR;

    uint32_t type = 0u;
    uint64_t size = 0u;
    result = port->fs_stat(port->ctx, normalized, &type, &size);
    if (result != MINI_OK) return result;
    if (type != MINI_FS_TYPE_FILE) return MINI_ERR_IS_DIR;

    result = port->fs_remove_file(port->ctx, normalized);
    if (result == MINI_OK) filesystem_quota_subtract(size);
    return result;
}

static mini_result_t fs_mkdir(const char *path)
{
    const minishell_services_port_t *port = minishell_services_port();
    if (!s_available) return MINI_ERR_UNSUPPORTED;
    if (port->fs_mkdir == NULL) return MINI_ERR_UNSUPPORTED;

    char normalized[MINI_FS_NORMALIZED_PATH_MAX];
    mini_result_t result = filesystem_path_normalize(path, normalized);
    if (result != MINI_OK) return result;
    if (strcmp(normalized, "/") == 0) return MINI_ERR_EXISTS;
    return port->fs_mkdir(port->ctx, normalized);
}

static mini_result_t fs_rmdir(const char *path)
{
    const minishell_services_port_t *port = minishell_services_port();
    if (!s_available) return MINI_ERR_UNSUPPORTED;
    if (port->fs_rmdir == NULL) return MINI_ERR_UNSUPPORTED;

    char normalized[MINI_FS_NORMALIZED_PATH_MAX];
    mini_result_t result = filesystem_path_normalize(path, normalized);
    if (result != MINI_OK) return result;
    if (strcmp(normalized, "/") == 0) return MINI_ERR_ACCESS;

    uint32_t type = 0u;
    uint64_t size = 0u;
    result = port->fs_stat(port->ctx, normalized, &type, &size);
    if (result != MINI_OK) return result;
    if (type != MINI_FS_TYPE_DIRECTORY) return MINI_ERR_NOT_DIR;
    return port->fs_rmdir(port->ctx, normalized);
}

static mini_result_t fs_dir_open(const char *path, mini_dir_t *out_dir)
{
    const minishell_services_port_t *port = minishell_services_port();
    if (out_dir == NULL) return MINI_ERR_INVALID;
    *out_dir = MINI_DIR_INVALID;
    if (!s_available || port->fs_dir_open == NULL ||
        port->fs_dir_read == NULL || port->fs_dir_close == NULL) {
        return MINI_ERR_UNSUPPORTED;
    }

    char normalized[MINI_FS_NORMALIZED_PATH_MAX];
    mini_result_t result = filesystem_path_normalize(path, normalized);
    if (result != MINI_OK) return result;

    uint32_t index = filesystem_handles_find_free_dir();
    if (index == MINI_FS_MAX_OPEN_DIRS) return MINI_ERR_TOO_MANY_OPEN;

    minishell_backend_dir_t backend = MINISHELL_BACKEND_DIR_INVALID;
    result = port->fs_dir_open(port->ctx, normalized, &backend);
    if (result != MINI_OK) return result;
    if (backend == MINISHELL_BACKEND_DIR_INVALID) return MINI_ERR_IO;

    *out_dir = filesystem_handles_activate_dir(index, backend);
    if (*out_dir == MINI_DIR_INVALID) {
        (void)port->fs_dir_close(port->ctx, backend);
        return MINI_ERR_IO;
    }
    return MINI_OK;
}

static mini_result_t fs_dir_read(mini_dir_t dir,
                                 mini_fs_dir_entry_t *out_entry,
                                 uint32_t *out_has_entry)
{
    const minishell_services_port_t *port = minishell_services_port();
    const uint32_t v0_size = MINI_FIELD_END(mini_fs_dir_entry_t, name);
    if (out_entry == NULL || out_has_entry == NULL ||
        out_entry->struct_size < v0_size) {
        return MINI_ERR_INVALID;
    }

    *out_has_entry = 0u;
    out_entry->type = 0u;
    out_entry->name[0] = '\0';
    if (!s_available || port->fs_dir_read == NULL) return MINI_ERR_UNSUPPORTED;

    filesystem_dir_slot_t *slot = filesystem_handles_lookup_dir(dir);
    if (slot == NULL) return MINI_ERR_BAD_HANDLE;

    for (;;) {
        char name[MINI_FS_NAME_MAX + 1u] = {0};
        uint32_t type = 0u;
        uint32_t has_entry = 0u;
        mini_result_t result = port->fs_dir_read(port->ctx, slot->backend,
                                                 name, (uint32_t)sizeof(name),
                                                 &type, &has_entry);
        if (result != MINI_OK) return result;
        if (has_entry == 0u) return MINI_OK;

        name[MINI_FS_NAME_MAX] = '\0';
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
        if (type != MINI_FS_TYPE_FILE && type != MINI_FS_TYPE_DIRECTORY) continue;

        size_t length = strlen(name);
        if (length > MINI_FS_NAME_MAX) return MINI_ERR_NAME_TOO_LONG;
        memcpy(out_entry->name, name, length + 1u);
        out_entry->type = type;
        *out_has_entry = 1u;
        return MINI_OK;
    }
}

static mini_result_t fs_dir_close(mini_dir_t dir)
{
    const minishell_services_port_t *port = minishell_services_port();
    if (!s_available || port->fs_dir_close == NULL) return MINI_ERR_UNSUPPORTED;

    filesystem_dir_slot_t *slot = filesystem_handles_lookup_dir(dir);
    if (slot == NULL) return MINI_ERR_BAD_HANDLE;

    minishell_backend_dir_t backend = filesystem_handles_release_dir(slot);
    return port->fs_dir_close(port->ctx, backend);
}

static mini_result_t fs_space(const char *path, mini_fs_space_t *out_space)
{
    const minishell_services_port_t *port = minishell_services_port();
    const uint32_t v0_size = MINI_FIELD_END(mini_fs_space_t, free_bytes);
    if (!s_available) return MINI_ERR_UNSUPPORTED;
    if (path == NULL || out_space == NULL || out_space->struct_size < v0_size) {
        return MINI_ERR_INVALID;
    }

    uint64_t limit = minishell_storage_limit_bytes();
    if (limit == 0u) return MINI_ERR_UNSUPPORTED;

    char normalized[MINI_FS_NORMALIZED_PATH_MAX];
    mini_result_t result = filesystem_path_normalize(path, normalized);
    if (result != MINI_OK) return result;

    uint32_t type = 0u;
    uint64_t size = 0u;
    result = port->fs_stat(port->ctx, normalized, &type, &size);
    if (result != MINI_OK) return result;

    result = filesystem_quota_refresh();
    if (result != MINI_OK) return result;

    out_space->reserved0 = 0u;
    out_space->total_bytes = limit;
    out_space->used_bytes = filesystem_quota_used();
    out_space->free_bytes = filesystem_quota_free(limit);
    return MINI_OK;
}

static const mini_fs_api_t s_fs_api = {
    .struct_size = sizeof(mini_fs_api_t),
    .open = fs_open,
    .close = fs_close,
    .read = fs_read,
    .write = fs_write,
    .seek = fs_seek,
    .sync = fs_sync,
    .stat = fs_stat,
    .rename = fs_rename,
    .remove_file = fs_remove_file,
    .mkdir = fs_mkdir,
    .rmdir = fs_rmdir,
    .dir_open = fs_dir_open,
    .dir_read = fs_dir_read,
    .dir_close = fs_dir_close,
    .space = fs_space,
};

void minishell_filesystem_service_configure(void)
{
    const minishell_services_port_t *port = minishell_services_port();
    filesystem_handles_reset();
    filesystem_quota_reset();

    s_available = port->fs_open != NULL && port->fs_close != NULL &&
                  port->fs_read != NULL && port->fs_write != NULL &&
                  port->fs_seek != NULL && port->fs_sync != NULL &&
                  port->fs_stat != NULL;
    if (s_available && minishell_storage_limit_bytes() != 0u) {
        (void)filesystem_quota_refresh();
    }
}

void minishell_filesystem_service_app_begin(void)
{
    minishell_filesystem_service_app_end();
    filesystem_handles_advance_generation();
}

void minishell_filesystem_service_app_end(void)
{
    filesystem_handles_close_all(minishell_services_port());
}

bool minishell_filesystem_service_available(void)
{
    return s_available;
}

const mini_fs_api_t *minishell_filesystem_service_api(void)
{
    return &s_fs_api;
}

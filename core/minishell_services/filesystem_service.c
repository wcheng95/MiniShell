#include <string.h>

#include "services_internal.h"

#define MINI_FS_MAX_OPEN_FILES 32u
#define MINI_FS_NORMALIZED_PATH_MAX 512u
#define MINI_FS_KNOWN_FLAGS (MINI_FS_READ | MINI_FS_WRITE | MINI_FS_CREATE | \
                             MINI_FS_EXCL | MINI_FS_TRUNC | MINI_FS_APPEND)

typedef struct {
    minishell_backend_file_t backend;
    uint16_t generation;
    uint32_t flags;
} file_slot_t;

static file_slot_t s_files[MINI_FS_MAX_OPEN_FILES];
static uint16_t s_generation = 1u;
static bool s_available;

static mini_file_t make_handle(uint32_t slot_index, uint16_t generation)
{
    return ((uint32_t)generation << 16) | (slot_index + 1u);
}

static file_slot_t *lookup_handle(mini_file_t file)
{
    if (file == MINI_FILE_INVALID) {
        return NULL;
    }
    uint32_t raw_slot = file & 0xFFFFu;
    uint16_t generation = (uint16_t)(file >> 16);
    if (raw_slot == 0u || raw_slot > MINI_FS_MAX_OPEN_FILES) {
        return NULL;
    }
    file_slot_t *slot = &s_files[raw_slot - 1u];
    if (slot->backend == MINISHELL_BACKEND_FILE_INVALID || slot->generation != generation) {
        return NULL;
    }
    return slot;
}

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

static mini_result_t normalize_path(const char *path, char *out, uint32_t out_size)
{
    if (path == NULL || out == NULL || out_size < 2u || path[0] != '/') return MINI_ERR_INVALID;
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
        } else if (len == 2u && start[0] == '.' && start[1] == '.') {
            if (out_len == 1u) return MINI_ERR_INVALID;
            if (out_len > 1u && out[out_len - 1u] == '/') --out_len;
            while (out_len > 1u && out[out_len - 1u] != '/') --out_len;
            if (out_len > 1u && out[out_len - 1u] == '/') --out_len;
            if (out_len == 0u) out_len = 1u;
            out[out_len] = '\0';
        } else if (len > 0u) {
            uint32_t need = out_len + (out_len > 1u ? 1u : 0u) + len + 1u;
            if (need > out_size) return MINI_ERR_NAME_TOO_LONG;
            if (out_len > 1u) out[out_len++] = '/';
            memcpy(&out[out_len], start, len);
            out_len += len;
            out[out_len] = '\0';
        }
        while (*p == '/') ++p;
    }
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
    result = normalize_path(path, normalized, sizeof(normalized));
    if (result != MINI_OK) return result;
    uint32_t index = MINI_FS_MAX_OPEN_FILES;
    for (uint32_t i = 0; i < MINI_FS_MAX_OPEN_FILES; ++i) {
        if (s_files[i].backend == MINISHELL_BACKEND_FILE_INVALID) { index = i; break; }
    }
    if (index == MINI_FS_MAX_OPEN_FILES) return MINI_ERR_TOO_MANY_OPEN;
    minishell_backend_file_t backend = MINISHELL_BACKEND_FILE_INVALID;
    result = port->fs_open(port->ctx, normalized, flags, &backend);
    if (result != MINI_OK) return result;
    if (backend == MINISHELL_BACKEND_FILE_INVALID) return MINI_ERR_IO;
    s_files[index].backend = backend;
    s_files[index].generation = s_generation;
    s_files[index].flags = flags;
    *out_file = make_handle(index, s_generation);
    return MINI_OK;
}

static mini_result_t fs_close(mini_file_t file)
{
    const minishell_services_port_t *port = minishell_services_port();
    file_slot_t *slot = lookup_handle(file);
    if (!s_available) return MINI_ERR_UNSUPPORTED;
    if (slot == NULL) return MINI_ERR_BAD_HANDLE;
    minishell_backend_file_t backend = slot->backend;
    slot->backend = MINISHELL_BACKEND_FILE_INVALID;
    slot->flags = 0u;
    return port->fs_close(port->ctx, backend);
}

static mini_result_t fs_read(mini_file_t file, void *buffer, uint32_t size, uint32_t *out_read)
{
    const minishell_services_port_t *port = minishell_services_port();
    if (out_read == NULL) return MINI_ERR_INVALID;
    *out_read = 0u;
    if (!s_available) return MINI_ERR_UNSUPPORTED;
    if (size > 0u && buffer == NULL) return MINI_ERR_INVALID;
    file_slot_t *slot = lookup_handle(file);
    if (slot == NULL) return MINI_ERR_BAD_HANDLE;
    if ((slot->flags & MINI_FS_READ) == 0u) return MINI_ERR_ACCESS;
    if (size == 0u) return MINI_OK;
    return port->fs_read(port->ctx, slot->backend, buffer, size, out_read);
}

static mini_result_t fs_write(mini_file_t file, const void *buffer, uint32_t size, uint32_t *out_written)
{
    const minishell_services_port_t *port = minishell_services_port();
    if (out_written == NULL) return MINI_ERR_INVALID;
    *out_written = 0u;
    if (!s_available) return MINI_ERR_UNSUPPORTED;
    if (size > 0u && buffer == NULL) return MINI_ERR_INVALID;
    file_slot_t *slot = lookup_handle(file);
    if (slot == NULL) return MINI_ERR_BAD_HANDLE;
    if ((slot->flags & MINI_FS_WRITE) == 0u) return MINI_ERR_ACCESS;
    if (size == 0u) return MINI_OK;
    mini_result_t result = port->fs_write(port->ctx, slot->backend, buffer, size, out_written);
    if (result == MINI_OK && *out_written == 0u) return MINI_ERR_IO;
    return result;
}

static mini_result_t fs_seek(mini_file_t file, int64_t offset, uint32_t origin, uint64_t *out_position)
{
    const minishell_services_port_t *port = minishell_services_port();
    if (out_position == NULL) return MINI_ERR_INVALID;
    *out_position = 0u;
    if (!s_available) return MINI_ERR_UNSUPPORTED;
    if (origin != MINI_FS_SEEK_SET && origin != MINI_FS_SEEK_CUR && origin != MINI_FS_SEEK_END) return MINI_ERR_INVALID;
    file_slot_t *slot = lookup_handle(file);
    if (slot == NULL) return MINI_ERR_BAD_HANDLE;
    return port->fs_seek(port->ctx, slot->backend, offset, origin, out_position);
}

static mini_result_t fs_sync(mini_file_t file)
{
    const minishell_services_port_t *port = minishell_services_port();
    if (!s_available) return MINI_ERR_UNSUPPORTED;
    file_slot_t *slot = lookup_handle(file);
    if (slot == NULL) return MINI_ERR_BAD_HANDLE;
    return port->fs_sync(port->ctx, slot->backend);
}

static mini_result_t fs_stat(const char *path, mini_fs_stat_t *out_stat)
{
    const minishell_services_port_t *port = minishell_services_port();
    const uint32_t v0_size = MINI_FIELD_END(mini_fs_stat_t, size);
    if (!s_available) return MINI_ERR_UNSUPPORTED;
    if (path == NULL || out_stat == NULL || out_stat->struct_size < v0_size) return MINI_ERR_INVALID;
    char normalized[MINI_FS_NORMALIZED_PATH_MAX];
    mini_result_t result = normalize_path(path, normalized, sizeof(normalized));
    if (result != MINI_OK) return result;
    uint32_t type = 0u;
    uint64_t size = 0u;
    result = port->fs_stat(port->ctx, normalized, &type, &size);
    if (result != MINI_OK) return result;
    out_stat->type = type;
    out_stat->size = size;
    return MINI_OK;
}

static const mini_fs_api_t s_fs_api = {
    .struct_size = sizeof(mini_fs_api_t), .open = fs_open, .close = fs_close,
    .read = fs_read, .write = fs_write, .seek = fs_seek, .sync = fs_sync, .stat = fs_stat,
};

void minishell_filesystem_service_configure(void)
{
    const minishell_services_port_t *port = minishell_services_port();
    memset(s_files, 0, sizeof(s_files));
    s_available = port->fs_open != NULL && port->fs_close != NULL && port->fs_read != NULL &&
                  port->fs_write != NULL && port->fs_seek != NULL && port->fs_sync != NULL && port->fs_stat != NULL;
}

void minishell_filesystem_service_app_begin(void)
{
    minishell_filesystem_service_app_end();
    ++s_generation;
    if (s_generation == 0u) s_generation = 1u;
}

void minishell_filesystem_service_app_end(void)
{
    const minishell_services_port_t *port = minishell_services_port();
    for (uint32_t i = 0; i < MINI_FS_MAX_OPEN_FILES; ++i) {
        if (s_files[i].backend != MINISHELL_BACKEND_FILE_INVALID) {
            if (port->fs_close != NULL) (void)port->fs_close(port->ctx, s_files[i].backend);
            s_files[i].backend = MINISHELL_BACKEND_FILE_INVALID;
            s_files[i].flags = 0u;
        }
    }
}

bool minishell_filesystem_service_available(void) { return s_available; }
const mini_fs_api_t *minishell_filesystem_service_api(void) { return &s_fs_api; }

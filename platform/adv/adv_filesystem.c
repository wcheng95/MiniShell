#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "esp_littlefs.h"

#include "adv_internal.h"

#define ADV_FLASH_PATH "/flash"
#define ADV_FLASH_LABEL "flash"
#define ADV_DIR_MAGIC 0x41445644u

typedef enum {
    ADV_DIR_ROOT = 1,
    ADV_DIR_NATIVE = 2,
} adv_dir_kind_t;

typedef struct {
    uint32_t magic;
    adv_dir_kind_t kind;
    uint32_t root_index;
    DIR *native;
    char path[MINI_FS_NORMALIZED_PATH_MAX];
} adv_dir_handle_t;

static bool s_flash_mounted;

static mini_result_t result_from_errno(int error)
{
    switch (error) {
        case 0: return MINI_OK;
        case EINVAL: return MINI_ERR_INVALID;
        case ENOENT: return MINI_ERR_NOT_FOUND;
        case EEXIST: return MINI_ERR_EXISTS;
        case EACCES:
        case EPERM:
        case EROFS: return MINI_ERR_ACCESS;
        case ENOSPC: return MINI_ERR_NO_SPACE;
        case EMFILE:
        case ENFILE: return MINI_ERR_TOO_MANY_OPEN;
        case ENAMETOOLONG: return MINI_ERR_NAME_TOO_LONG;
        case ENOTDIR: return MINI_ERR_NOT_DIR;
        case EISDIR: return MINI_ERR_IS_DIR;
        case ENOMEM: return MINI_ERR_NO_MEMORY;
#ifdef ENOTEMPTY
        case ENOTEMPTY: return MINI_ERR_NOT_EMPTY;
#endif
#ifdef EXDEV
        case EXDEV: return MINI_ERR_UNSUPPORTED;
#endif
        default: return MINI_ERR_IO;
    }
}

static bool is_flash_path(const char *path)
{
    if (!s_flash_mounted || path == NULL) return false;
    if (strcmp(path, ADV_FLASH_PATH) == 0) return true;
    return strncmp(path, ADV_FLASH_PATH "/", sizeof(ADV_FLASH_PATH)) == 0;
}

static minishell_backend_file_t file_handle_from_fd(int fd)
{
    return (minishell_backend_file_t)((uintptr_t)fd + 1u);
}

static int fd_from_file_handle(minishell_backend_file_t file)
{
    if (file == MINISHELL_BACKEND_FILE_INVALID ||
        file > (minishell_backend_file_t)((uintptr_t)INT_MAX + 1u)) {
        return -1;
    }
    return (int)(file - 1u);
}

static adv_dir_handle_t *dir_from_handle(minishell_backend_dir_t handle)
{
    if (handle == MINISHELL_BACKEND_DIR_INVALID) return NULL;
    adv_dir_handle_t *dir = (adv_dir_handle_t *)(uintptr_t)handle;
    return dir->magic == ADV_DIR_MAGIC ? dir : NULL;
}

static mini_result_t fs_open(void *ctx, const char *path, uint32_t flags,
                             minishell_backend_file_t *out_file)
{
    (void)ctx;
    if (out_file == NULL || path == NULL) return MINI_ERR_INVALID;
    *out_file = MINISHELL_BACKEND_FILE_INVALID;
    if (!is_flash_path(path)) return MINI_ERR_NOT_FOUND;
    if (strcmp(path, ADV_FLASH_PATH) == 0) return MINI_ERR_IS_DIR;

    int oflags;
    if ((flags & MINI_FS_READ) != 0u && (flags & MINI_FS_WRITE) != 0u) oflags = O_RDWR;
    else if ((flags & MINI_FS_WRITE) != 0u) oflags = O_WRONLY;
    else oflags = O_RDONLY;

    if ((flags & MINI_FS_CREATE) != 0u) oflags |= O_CREAT;
    if ((flags & MINI_FS_EXCL) != 0u) oflags |= O_EXCL;
    if ((flags & MINI_FS_TRUNC) != 0u) oflags |= O_TRUNC;
    if ((flags & MINI_FS_APPEND) != 0u) oflags |= O_APPEND;

    int fd = open(path, oflags, 0666);
    if (fd < 0) return result_from_errno(errno);

    struct stat st;
    if (fstat(fd, &st) != 0) {
        int error = errno;
        (void)close(fd);
        return result_from_errno(error);
    }
    if (S_ISDIR(st.st_mode)) {
        (void)close(fd);
        return MINI_ERR_IS_DIR;
    }
    if (!S_ISREG(st.st_mode)) {
        (void)close(fd);
        return MINI_ERR_UNSUPPORTED;
    }

    *out_file = file_handle_from_fd(fd);
    return MINI_OK;
}

static mini_result_t fs_close(void *ctx, minishell_backend_file_t file)
{
    (void)ctx;
    int fd = fd_from_file_handle(file);
    if (fd < 0) return MINI_ERR_BAD_HANDLE;
    return close(fd) == 0 ? MINI_OK : result_from_errno(errno);
}

static mini_result_t fs_read(void *ctx, minishell_backend_file_t file,
                             void *buffer, uint32_t size, uint32_t *out_read)
{
    (void)ctx;
    int fd = fd_from_file_handle(file);
    if (fd < 0) return MINI_ERR_BAD_HANDLE;
    if (out_read == NULL) return MINI_ERR_INVALID;
    ssize_t count = read(fd, buffer, (size_t)size);
    if (count < 0) return result_from_errno(errno);
    *out_read = (uint32_t)count;
    return MINI_OK;
}

static mini_result_t fs_write(void *ctx, minishell_backend_file_t file,
                              const void *buffer, uint32_t size,
                              uint32_t *out_written)
{
    (void)ctx;
    int fd = fd_from_file_handle(file);
    if (fd < 0) return MINI_ERR_BAD_HANDLE;
    if (out_written == NULL) return MINI_ERR_INVALID;
    ssize_t count = write(fd, buffer, (size_t)size);
    if (count < 0) return result_from_errno(errno);
    *out_written = (uint32_t)count;
    return MINI_OK;
}

static mini_result_t fs_seek(void *ctx, minishell_backend_file_t file,
                             int64_t offset, uint32_t origin,
                             uint64_t *out_position)
{
    (void)ctx;
    int fd = fd_from_file_handle(file);
    if (fd < 0) return MINI_ERR_BAD_HANDLE;
    if (out_position == NULL) return MINI_ERR_INVALID;

    int whence;
    switch (origin) {
        case MINI_FS_SEEK_SET: whence = SEEK_SET; break;
        case MINI_FS_SEEK_CUR: whence = SEEK_CUR; break;
        case MINI_FS_SEEK_END: whence = SEEK_END; break;
        default: return MINI_ERR_INVALID;
    }

    off_t position = lseek(fd, (off_t)offset, whence);
    if (position < 0) return result_from_errno(errno);
    *out_position = (uint64_t)position;
    return MINI_OK;
}

static mini_result_t fs_sync(void *ctx, minishell_backend_file_t file)
{
    (void)ctx;
    int fd = fd_from_file_handle(file);
    if (fd < 0) return MINI_ERR_BAD_HANDLE;
    return fsync(fd) == 0 ? MINI_OK : result_from_errno(errno);
}

static mini_result_t fs_stat(void *ctx, const char *path,
                             uint32_t *out_type, uint64_t *out_size)
{
    (void)ctx;
    if (path == NULL || out_type == NULL || out_size == NULL) return MINI_ERR_INVALID;

    if (strcmp(path, "/") == 0) {
        *out_type = MINI_FS_TYPE_DIRECTORY;
        *out_size = 0u;
        return MINI_OK;
    }
    if (!is_flash_path(path)) return MINI_ERR_NOT_FOUND;
    if (strcmp(path, ADV_FLASH_PATH) == 0) {
        *out_type = MINI_FS_TYPE_DIRECTORY;
        *out_size = 0u;
        return MINI_OK;
    }

    struct stat st;
    if (stat(path, &st) != 0) return result_from_errno(errno);
    if (S_ISREG(st.st_mode)) *out_type = MINI_FS_TYPE_FILE;
    else if (S_ISDIR(st.st_mode)) *out_type = MINI_FS_TYPE_DIRECTORY;
    else return MINI_ERR_UNSUPPORTED;
    *out_size = (uint64_t)st.st_size;
    return MINI_OK;
}

static mini_result_t fs_rename(void *ctx, const char *old_path,
                               const char *new_path)
{
    (void)ctx;
    if (!is_flash_path(old_path) || !is_flash_path(new_path)) {
        return MINI_ERR_UNSUPPORTED;
    }
    return rename(old_path, new_path) == 0 ? MINI_OK : result_from_errno(errno);
}

static mini_result_t fs_remove_file(void *ctx, const char *path)
{
    (void)ctx;
    if (!is_flash_path(path)) return MINI_ERR_NOT_FOUND;
    return unlink(path) == 0 ? MINI_OK : result_from_errno(errno);
}

static mini_result_t fs_mkdir(void *ctx, const char *path)
{
    (void)ctx;
    if (!is_flash_path(path)) return MINI_ERR_NOT_FOUND;
    return mkdir(path, 0777) == 0 ? MINI_OK : result_from_errno(errno);
}

static mini_result_t fs_rmdir(void *ctx, const char *path)
{
    (void)ctx;
    if (!is_flash_path(path)) return MINI_ERR_NOT_FOUND;
    return rmdir(path) == 0 ? MINI_OK : result_from_errno(errno);
}

static mini_result_t fs_dir_open(void *ctx, const char *path,
                                 minishell_backend_dir_t *out_dir)
{
    (void)ctx;
    if (path == NULL || out_dir == NULL) return MINI_ERR_INVALID;
    *out_dir = MINISHELL_BACKEND_DIR_INVALID;

    adv_dir_handle_t *handle = (adv_dir_handle_t *)calloc(1u, sizeof(*handle));
    if (handle == NULL) return MINI_ERR_NO_MEMORY;
    handle->magic = ADV_DIR_MAGIC;

    if (strcmp(path, "/") == 0) {
        handle->kind = ADV_DIR_ROOT;
        *out_dir = (minishell_backend_dir_t)(uintptr_t)handle;
        return MINI_OK;
    }
    if (!is_flash_path(path)) {
        free(handle);
        return MINI_ERR_NOT_FOUND;
    }

    DIR *dir = opendir(path);
    if (dir == NULL) {
        int error = errno;
        free(handle);
        return result_from_errno(error);
    }

    size_t length = strlen(path);
    if (length >= sizeof(handle->path)) {
        (void)closedir(dir);
        free(handle);
        return MINI_ERR_NAME_TOO_LONG;
    }
    memcpy(handle->path, path, length + 1u);
    handle->kind = ADV_DIR_NATIVE;
    handle->native = dir;
    *out_dir = (minishell_backend_dir_t)(uintptr_t)handle;
    return MINI_OK;
}

static mini_result_t fs_dir_read(void *ctx, minishell_backend_dir_t dir,
                                 char *out_name, uint32_t name_size,
                                 uint32_t *out_type, uint32_t *out_has_entry)
{
    (void)ctx;
    if (out_name == NULL || name_size == 0u || out_type == NULL ||
        out_has_entry == NULL) {
        return MINI_ERR_INVALID;
    }
    out_name[0] = '\0';
    *out_type = 0u;
    *out_has_entry = 0u;

    adv_dir_handle_t *handle = dir_from_handle(dir);
    if (handle == NULL) return MINI_ERR_BAD_HANDLE;

    if (handle->kind == ADV_DIR_ROOT) {
        if (handle->root_index++ != 0u || !s_flash_mounted) return MINI_OK;
        if (name_size < sizeof("flash")) return MINI_ERR_NAME_TOO_LONG;
        memcpy(out_name, "flash", sizeof("flash"));
        *out_type = MINI_FS_TYPE_DIRECTORY;
        *out_has_entry = 1u;
        return MINI_OK;
    }

    for (;;) {
        errno = 0;
        struct dirent *entry = readdir(handle->native);
        if (entry == NULL) return errno == 0 ? MINI_OK : result_from_errno(errno);
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

        size_t name_length = strlen(entry->d_name);
        if (name_length + 1u > name_size) return MINI_ERR_NAME_TOO_LONG;

        char child[MINI_FS_NORMALIZED_PATH_MAX];
        int written = snprintf(child, sizeof(child), "%s/%s", handle->path, entry->d_name);
        if (written < 0 || (size_t)written >= sizeof(child)) return MINI_ERR_NAME_TOO_LONG;

        struct stat st;
        if (stat(child, &st) != 0) return result_from_errno(errno);
        if (S_ISREG(st.st_mode)) *out_type = MINI_FS_TYPE_FILE;
        else if (S_ISDIR(st.st_mode)) *out_type = MINI_FS_TYPE_DIRECTORY;
        else continue;

        memcpy(out_name, entry->d_name, name_length + 1u);
        *out_has_entry = 1u;
        return MINI_OK;
    }
}

static mini_result_t fs_dir_close(void *ctx, minishell_backend_dir_t dir)
{
    (void)ctx;
    adv_dir_handle_t *handle = dir_from_handle(dir);
    if (handle == NULL) return MINI_ERR_BAD_HANDLE;

    mini_result_t result = MINI_OK;
    if (handle->kind == ADV_DIR_NATIVE && handle->native != NULL &&
        closedir(handle->native) != 0) {
        result = result_from_errno(errno);
    }
    handle->magic = 0u;
    free(handle);
    return result;
}

int adv_filesystem_prepare(void)
{
    esp_vfs_littlefs_conf_t config = {
        .base_path = ADV_FLASH_PATH,
        .partition_label = ADV_FLASH_LABEL,
        .format_if_mount_failed = true,
        .dont_mount = false,
        .grow_on_mount = true,
    };

    esp_err_t error = esp_vfs_littlefs_register(&config);
    if (error != ESP_OK) {
        s_flash_mounted = false;
        return -1;
    }
    s_flash_mounted = true;

    if (mkdir(ADV_FLASH_PATH "/minishell", 0777) != 0 && errno != EEXIST) {
        (void)esp_vfs_littlefs_unregister(ADV_FLASH_LABEL);
        s_flash_mounted = false;
        return -1;
    }
    return 0;
}

void adv_filesystem_shutdown(void)
{
    if (!s_flash_mounted) return;
    (void)esp_vfs_littlefs_unregister(ADV_FLASH_LABEL);
    s_flash_mounted = false;
}

bool adv_filesystem_flash_ready(void)
{
    return s_flash_mounted;
}

void adv_filesystem_configure(minishell_services_port_t *port)
{
    if (port == NULL || !s_flash_mounted) return;
    port->fs_open = fs_open;
    port->fs_close = fs_close;
    port->fs_read = fs_read;
    port->fs_write = fs_write;
    port->fs_seek = fs_seek;
    port->fs_sync = fs_sync;
    port->fs_stat = fs_stat;
    port->fs_rename = fs_rename;
    port->fs_remove_file = fs_remove_file;
    port->fs_mkdir = fs_mkdir;
    port->fs_rmdir = fs_rmdir;
    port->fs_dir_open = fs_dir_open;
    port->fs_dir_read = fs_dir_read;
    port->fs_dir_close = fs_dir_close;
}

#define _GNU_SOURCE

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "linux_internal.h"

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

static minishell_backend_dir_t dir_handle_from_ptr(DIR *dir)
{
    return (minishell_backend_dir_t)(uintptr_t)dir;
}

static DIR *dir_from_handle(minishell_backend_dir_t dir)
{
    if (dir == MINISHELL_BACKEND_DIR_INVALID) return NULL;
    return (DIR *)(uintptr_t)dir;
}

static mini_result_t fs_open(void *ctx, const char *path, uint32_t flags,
                             minishell_backend_file_t *out_file)
{
    (void)ctx;
    if (out_file == NULL) return MINI_ERR_INVALID;
    *out_file = MINISHELL_BACKEND_FILE_INVALID;

    char native[PATH_MAX];
    mini_result_t result = linux_host_path(path, native, sizeof(native));
    if (result != MINI_OK) return result;

    int oflags;
    if ((flags & MINI_FS_READ) != 0u && (flags & MINI_FS_WRITE) != 0u) oflags = O_RDWR;
    else if ((flags & MINI_FS_WRITE) != 0u) oflags = O_WRONLY;
    else oflags = O_RDONLY;

    if ((flags & MINI_FS_CREATE) != 0u) oflags |= O_CREAT;
    if ((flags & MINI_FS_EXCL) != 0u) oflags |= O_EXCL;
    if ((flags & MINI_FS_TRUNC) != 0u) oflags |= O_TRUNC;
    if ((flags & MINI_FS_APPEND) != 0u) oflags |= O_APPEND;

    int fd;
    do {
        fd = open(native, oflags, 0666);
    } while (fd < 0 && errno == EINTR);
    if (fd < 0) return linux_result_from_errno(errno);

    struct stat st;
    if (fstat(fd, &st) != 0) {
        int error = errno;
        (void)close(fd);
        return linux_result_from_errno(error);
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

    int result;
    do {
        result = close(fd);
    } while (result != 0 && errno == EINTR);
    return result == 0 ? MINI_OK : linux_result_from_errno(errno);
}

static mini_result_t fs_read(void *ctx, minishell_backend_file_t file,
                             void *buffer, uint32_t size, uint32_t *out_read)
{
    (void)ctx;
    int fd = fd_from_file_handle(file);
    if (fd < 0) return MINI_ERR_BAD_HANDLE;
    if (out_read == NULL) return MINI_ERR_INVALID;

    ssize_t count;
    do {
        count = read(fd, buffer, (size_t)size);
    } while (count < 0 && errno == EINTR);
    if (count < 0) return linux_result_from_errno(errno);
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

    ssize_t count;
    do {
        count = write(fd, buffer, (size_t)size);
    } while (count < 0 && errno == EINTR);
    if (count < 0) return linux_result_from_errno(errno);
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
    if (position < 0) return linux_result_from_errno(errno);
    *out_position = (uint64_t)position;
    return MINI_OK;
}

static mini_result_t fs_sync(void *ctx, minishell_backend_file_t file)
{
    (void)ctx;
    int fd = fd_from_file_handle(file);
    if (fd < 0) return MINI_ERR_BAD_HANDLE;

    int result;
    do {
        result = fsync(fd);
    } while (result != 0 && errno == EINTR);
    return result == 0 ? MINI_OK : linux_result_from_errno(errno);
}

static mini_result_t fs_stat(void *ctx, const char *path,
                             uint32_t *out_type, uint64_t *out_size)
{
    (void)ctx;
    if (out_type == NULL || out_size == NULL) return MINI_ERR_INVALID;

    char native[PATH_MAX];
    mini_result_t result = linux_host_path(path, native, sizeof(native));
    if (result != MINI_OK) return result;

    struct stat st;
    if (stat(native, &st) != 0) return linux_result_from_errno(errno);
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
    char old_native[PATH_MAX];
    char new_native[PATH_MAX];
    mini_result_t result = linux_host_path(old_path, old_native, sizeof(old_native));
    if (result != MINI_OK) return result;
    result = linux_host_path(new_path, new_native, sizeof(new_native));
    if (result != MINI_OK) return result;
    return rename(old_native, new_native) == 0 ? MINI_OK : linux_result_from_errno(errno);
}

static mini_result_t fs_remove_file(void *ctx, const char *path)
{
    (void)ctx;
    char native[PATH_MAX];
    mini_result_t result = linux_host_path(path, native, sizeof(native));
    if (result != MINI_OK) return result;
    return unlink(native) == 0 ? MINI_OK : linux_result_from_errno(errno);
}

static mini_result_t fs_mkdir(void *ctx, const char *path)
{
    (void)ctx;
    char native[PATH_MAX];
    mini_result_t result = linux_host_path(path, native, sizeof(native));
    if (result != MINI_OK) return result;
    return mkdir(native, 0777) == 0 ? MINI_OK : linux_result_from_errno(errno);
}

static mini_result_t fs_rmdir(void *ctx, const char *path)
{
    (void)ctx;
    char native[PATH_MAX];
    mini_result_t result = linux_host_path(path, native, sizeof(native));
    if (result != MINI_OK) return result;
    return rmdir(native) == 0 ? MINI_OK : linux_result_from_errno(errno);
}

static mini_result_t fs_dir_open(void *ctx, const char *path,
                                 minishell_backend_dir_t *out_dir)
{
    (void)ctx;
    if (out_dir == NULL) return MINI_ERR_INVALID;
    *out_dir = MINISHELL_BACKEND_DIR_INVALID;

    char native[PATH_MAX];
    mini_result_t result = linux_host_path(path, native, sizeof(native));
    if (result != MINI_OK) return result;

    DIR *dir = opendir(native);
    if (dir == NULL) return linux_result_from_errno(errno);
    *out_dir = dir_handle_from_ptr(dir);
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

    DIR *native_dir = dir_from_handle(dir);
    if (native_dir == NULL) return MINI_ERR_BAD_HANDLE;

    for (;;) {
        errno = 0;
        struct dirent *entry = readdir(native_dir);
        if (entry == NULL) return errno == 0 ? MINI_OK : linux_result_from_errno(errno);

        struct stat st;
        int fd = dirfd(native_dir);
        if (fd < 0) return linux_result_from_errno(errno);
        if (fstatat(fd, entry->d_name, &st, AT_SYMLINK_NOFOLLOW) != 0) {
            return linux_result_from_errno(errno);
        }

        uint32_t type;
        if (S_ISREG(st.st_mode)) type = MINI_FS_TYPE_FILE;
        else if (S_ISDIR(st.st_mode)) type = MINI_FS_TYPE_DIRECTORY;
        else continue;

        size_t length = strlen(entry->d_name);
        if (length + 1u > (size_t)name_size) return MINI_ERR_NAME_TOO_LONG;
        memcpy(out_name, entry->d_name, length + 1u);
        *out_type = type;
        *out_has_entry = 1u;
        return MINI_OK;
    }
}

static mini_result_t fs_dir_close(void *ctx, minishell_backend_dir_t dir)
{
    (void)ctx;
    DIR *native_dir = dir_from_handle(dir);
    if (native_dir == NULL) return MINI_ERR_BAD_HANDLE;
    return closedir(native_dir) == 0 ? MINI_OK : linux_result_from_errno(errno);
}

void linux_filesystem_configure(minishell_services_port_t *port)
{
    if (port == NULL) return;
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

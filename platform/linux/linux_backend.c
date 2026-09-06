#define _GNU_SOURCE

#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "minishell_services.h"
#include "platform_backend.h"

#define APP_NAME_MAX 128
#define INPUT_READ_MAX 64

typedef struct {
    char app_dir[PATH_MAX];
    char root_dir[PATH_MAX];
    struct termios saved_termios;
    bool terminal_mode_active;
} linux_state_t;

static linux_state_t s_state;
static minishell_services_port_t s_services_port;

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
        case ENOSPC:
#ifdef EDQUOT
        case EDQUOT:
#endif
            return MINI_ERR_NO_SPACE;
        case EMFILE:
        case ENFILE: return MINI_ERR_TOO_MANY_OPEN;
        case ENAMETOOLONG: return MINI_ERR_NAME_TOO_LONG;
        case ENOTDIR: return MINI_ERR_NOT_DIR;
        case EISDIR: return MINI_ERR_IS_DIR;
        case ENOMEM: return MINI_ERR_NO_MEMORY;
#ifdef ENOTEMPTY
        case ENOTEMPTY: return MINI_ERR_NOT_EMPTY;
#endif
        default: return MINI_ERR_IO;
    }
}

static int mkdir_one(const char *path)
{
    if (mkdir(path, 0777) == 0) {
        return 0;
    }
    if (errno != EEXIST) {
        return -errno;
    }

    struct stat st;
    if (stat(path, &st) != 0) {
        return -errno;
    }
    return S_ISDIR(st.st_mode) ? 0 : -ENOTDIR;
}

static int mkdir_p(const char *path)
{
    char temp[PATH_MAX];
    size_t length = strlen(path);
    if (length == 0 || length >= sizeof(temp)) {
        return -ENAMETOOLONG;
    }

    memcpy(temp, path, length + 1);
    for (char *p = temp + 1; *p != '\0'; ++p) {
        if (*p != '/') {
            continue;
        }
        *p = '\0';
        int ret = mkdir_one(temp);
        *p = '/';
        if (ret != 0) {
            return ret;
        }
    }
    return mkdir_one(temp);
}

static int build_default_paths(void)
{
    char executable[PATH_MAX];
    ssize_t length = readlink("/proc/self/exe", executable, sizeof(executable) - 1);
    if (length < 0 || (size_t)length >= sizeof(executable) - 1) {
        return -errno;
    }
    executable[length] = '\0';

    char *slash = strrchr(executable, '/');
    if (slash == NULL) {
        return -EINVAL;
    }
    *slash = '\0';

    int written = snprintf(s_state.app_dir, sizeof(s_state.app_dir),
                           "%s/runtime/apps", executable);
    if (written < 0 || (size_t)written >= sizeof(s_state.app_dir)) {
        return -ENAMETOOLONG;
    }

    const char *root_override = getenv("MINISHELL_ROOT");
    if (root_override != NULL && root_override[0] != '\0') {
        written = snprintf(s_state.root_dir, sizeof(s_state.root_dir), "%s", root_override);
    } else {
        const char *xdg = getenv("XDG_DATA_HOME");
        if (xdg != NULL && xdg[0] != '\0') {
            written = snprintf(s_state.root_dir, sizeof(s_state.root_dir),
                               "%s/minishell/fs", xdg);
        } else {
            const char *home = getenv("HOME");
            if (home == NULL || home[0] == '\0') {
                return -ENOENT;
            }
            written = snprintf(s_state.root_dir, sizeof(s_state.root_dir),
                               "%s/.local/share/minishell/fs", home);
        }
    }

    if (written < 0 || (size_t)written >= sizeof(s_state.root_dir)) {
        return -ENAMETOOLONG;
    }
    return 0;
}

static int prepare_logical_root(void)
{
    int ret = mkdir_p(s_state.root_dir);
    if (ret != 0) {
        return ret;
    }

    const char *children[] = {"sd", "flash", ".state"};
    for (size_t i = 0; i < sizeof(children) / sizeof(children[0]); ++i) {
        char path[PATH_MAX];
        int written = snprintf(path, sizeof(path), "%s/%s",
                               s_state.root_dir, children[i]);
        if (written < 0 || (size_t)written >= sizeof(path)) {
            return -ENAMETOOLONG;
        }
        ret = mkdir_p(path);
        if (ret != 0) {
            return ret;
        }
    }
    return 0;
}

static const char *app_dir(void)
{
    const char *override = getenv("MINISHELL_APP_DIR");
    return (override != NULL && override[0] != '\0') ? override : s_state.app_dir;
}

static mini_result_t host_path(const char *logical, char *out, size_t out_size)
{
    if (logical == NULL || logical[0] != '/' || out == NULL || out_size == 0) {
        return MINI_ERR_INVALID;
    }

    int written;
    if (strcmp(logical, "/") == 0) {
        written = snprintf(out, out_size, "%s", s_state.root_dir);
    } else {
        written = snprintf(out, out_size, "%s%s", s_state.root_dir, logical);
    }
    if (written < 0 || (size_t)written >= out_size) {
        return MINI_ERR_NAME_TOO_LONG;
    }
    return MINI_OK;
}

void minishell_platform_write(const char *text)
{
    if (text != NULL) {
        fputs(text, stdout);
        fflush(stdout);
    }
}

static void service_system_write(void *ctx, const char *text)
{
    (void)ctx;
    minishell_platform_write(text);
}

static void *service_memory_alloc(void *ctx, uint32_t size)
{
    (void)ctx;
    return malloc((size_t)size);
}

static void *service_memory_realloc(void *ctx, void *ptr, uint32_t new_size)
{
    (void)ctx;
    return realloc(ptr, (size_t)new_size);
}

static void service_memory_free(void *ctx, void *ptr)
{
    (void)ctx;
    free(ptr);
}

static bool service_memory_get_info(void *ctx, uint64_t *free_bytes,
                                    uint64_t *largest_free_block)
{
    (void)ctx;
    (void)free_bytes;
    (void)largest_free_block;

    /* Linux does not expose a meaningful process-wide "largest allocatable
     * block" comparable to an MCU heap.  The portable service still reports
     * exact per-application allocation usage. */
    return false;
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

static minishell_backend_dir_t dir_handle_from_ptr(DIR *dir)
{
    return (minishell_backend_dir_t)(uintptr_t)dir;
}

static DIR *dir_from_handle(minishell_backend_dir_t dir)
{
    if (dir == MINISHELL_BACKEND_DIR_INVALID) return NULL;
    return (DIR *)(uintptr_t)dir;
}

static mini_result_t service_fs_open(void *ctx, const char *path, uint32_t flags,
                                     minishell_backend_file_t *out_file)
{
    (void)ctx;
    if (out_file == NULL) {
        return MINI_ERR_INVALID;
    }
    *out_file = MINISHELL_BACKEND_FILE_INVALID;

    char native[PATH_MAX];
    mini_result_t result = host_path(path, native, sizeof(native));
    if (result != MINI_OK) {
        return result;
    }

    int oflags;
    if ((flags & MINI_FS_READ) != 0u && (flags & MINI_FS_WRITE) != 0u) {
        oflags = O_RDWR;
    } else if ((flags & MINI_FS_WRITE) != 0u) {
        oflags = O_WRONLY;
    } else {
        oflags = O_RDONLY;
    }
    if ((flags & MINI_FS_CREATE) != 0u) oflags |= O_CREAT;
    if ((flags & MINI_FS_EXCL) != 0u) oflags |= O_EXCL;
    if ((flags & MINI_FS_TRUNC) != 0u) oflags |= O_TRUNC;
    if ((flags & MINI_FS_APPEND) != 0u) oflags |= O_APPEND;

    int fd;
    do {
        fd = open(native, oflags, 0666);
    } while (fd < 0 && errno == EINTR);
    if (fd < 0) {
        return result_from_errno(errno);
    }

    struct stat st;
    if (fstat(fd, &st) != 0) {
        int error = errno;
        close(fd);
        return result_from_errno(error);
    }
    if (S_ISDIR(st.st_mode)) {
        close(fd);
        return MINI_ERR_IS_DIR;
    }
    if (!S_ISREG(st.st_mode)) {
        close(fd);
        return MINI_ERR_UNSUPPORTED;
    }

    *out_file = file_handle_from_fd(fd);
    return MINI_OK;
}

static mini_result_t service_fs_close(void *ctx, minishell_backend_file_t file)
{
    (void)ctx;
    int fd = fd_from_file_handle(file);
    if (fd < 0) return MINI_ERR_BAD_HANDLE;

    int ret;
    do {
        ret = close(fd);
    } while (ret != 0 && errno == EINTR);
    return ret == 0 ? MINI_OK : result_from_errno(errno);
}

static mini_result_t service_fs_read(void *ctx, minishell_backend_file_t file,
                                     void *buffer, uint32_t size, uint32_t *out_read)
{
    (void)ctx;
    int fd = fd_from_file_handle(file);
    if (fd < 0 || out_read == NULL) return MINI_ERR_BAD_HANDLE;

    ssize_t count;
    do {
        count = read(fd, buffer, (size_t)size);
    } while (count < 0 && errno == EINTR);
    if (count < 0) return result_from_errno(errno);
    *out_read = (uint32_t)count;
    return MINI_OK;
}

static mini_result_t service_fs_write(void *ctx, minishell_backend_file_t file,
                                      const void *buffer, uint32_t size,
                                      uint32_t *out_written)
{
    (void)ctx;
    int fd = fd_from_file_handle(file);
    if (fd < 0 || out_written == NULL) return MINI_ERR_BAD_HANDLE;

    ssize_t count;
    do {
        count = write(fd, buffer, (size_t)size);
    } while (count < 0 && errno == EINTR);
    if (count < 0) return result_from_errno(errno);
    *out_written = (uint32_t)count;
    return MINI_OK;
}

static mini_result_t service_fs_seek(void *ctx, minishell_backend_file_t file,
                                     int64_t offset, uint32_t origin,
                                     uint64_t *out_position)
{
    (void)ctx;
    int fd = fd_from_file_handle(file);
    if (fd < 0 || out_position == NULL) return MINI_ERR_BAD_HANDLE;

    int whence;
    switch (origin) {
        case MINI_FS_SEEK_SET: whence = SEEK_SET; break;
        case MINI_FS_SEEK_CUR: whence = SEEK_CUR; break;
        case MINI_FS_SEEK_END: whence = SEEK_END; break;
        default: return MINI_ERR_INVALID;
    }

    errno = 0;
    off_t position = lseek(fd, (off_t)offset, whence);
    if (position < 0) return result_from_errno(errno);
    *out_position = (uint64_t)position;
    return MINI_OK;
}

static mini_result_t service_fs_sync(void *ctx, minishell_backend_file_t file)
{
    (void)ctx;
    int fd = fd_from_file_handle(file);
    if (fd < 0) return MINI_ERR_BAD_HANDLE;

    int ret;
    do {
        ret = fsync(fd);
    } while (ret != 0 && errno == EINTR);
    return ret == 0 ? MINI_OK : result_from_errno(errno);
}

static mini_result_t service_fs_stat(void *ctx, const char *path,
                                     uint32_t *out_type, uint64_t *out_size)
{
    (void)ctx;
    if (out_type == NULL || out_size == NULL) return MINI_ERR_INVALID;

    char native[PATH_MAX];
    mini_result_t result = host_path(path, native, sizeof(native));
    if (result != MINI_OK) return result;

    struct stat st;
    if (stat(native, &st) != 0) return result_from_errno(errno);

    if (S_ISREG(st.st_mode)) *out_type = MINI_FS_TYPE_FILE;
    else if (S_ISDIR(st.st_mode)) *out_type = MINI_FS_TYPE_DIRECTORY;
    else return MINI_ERR_UNSUPPORTED;
    *out_size = (uint64_t)st.st_size;
    return MINI_OK;
}

static mini_result_t service_fs_rename(void *ctx, const char *old_path,
                                       const char *new_path)
{
    (void)ctx;
    char old_native[PATH_MAX];
    char new_native[PATH_MAX];
    mini_result_t result = host_path(old_path, old_native, sizeof(old_native));
    if (result != MINI_OK) return result;
    result = host_path(new_path, new_native, sizeof(new_native));
    if (result != MINI_OK) return result;
    return rename(old_native, new_native) == 0 ? MINI_OK : result_from_errno(errno);
}

static mini_result_t service_fs_remove_file(void *ctx, const char *path)
{
    (void)ctx;
    char native[PATH_MAX];
    mini_result_t result = host_path(path, native, sizeof(native));
    if (result != MINI_OK) return result;
    return unlink(native) == 0 ? MINI_OK : result_from_errno(errno);
}

static mini_result_t service_fs_mkdir(void *ctx, const char *path)
{
    (void)ctx;
    char native[PATH_MAX];
    mini_result_t result = host_path(path, native, sizeof(native));
    if (result != MINI_OK) return result;
    return mkdir(native, 0777) == 0 ? MINI_OK : result_from_errno(errno);
}

static mini_result_t service_fs_rmdir(void *ctx, const char *path)
{
    (void)ctx;
    char native[PATH_MAX];
    mini_result_t result = host_path(path, native, sizeof(native));
    if (result != MINI_OK) return result;
    return rmdir(native) == 0 ? MINI_OK : result_from_errno(errno);
}

static mini_result_t service_fs_dir_open(void *ctx, const char *path,
                                         minishell_backend_dir_t *out_dir)
{
    (void)ctx;
    if (out_dir == NULL) return MINI_ERR_INVALID;
    *out_dir = MINISHELL_BACKEND_DIR_INVALID;

    char native[PATH_MAX];
    mini_result_t result = host_path(path, native, sizeof(native));
    if (result != MINI_OK) return result;

    DIR *dir = opendir(native);
    if (dir == NULL) return result_from_errno(errno);
    *out_dir = dir_handle_from_ptr(dir);
    return MINI_OK;
}

static mini_result_t service_fs_dir_read(void *ctx, minishell_backend_dir_t dir,
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
        if (entry == NULL) {
            return errno == 0 ? MINI_OK : result_from_errno(errno);
        }

        struct stat st;
        int fd = dirfd(native_dir);
        if (fd < 0) return result_from_errno(errno);
        if (fstatat(fd, entry->d_name, &st, AT_SYMLINK_NOFOLLOW) != 0) {
            return result_from_errno(errno);
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

static mini_result_t service_fs_dir_close(void *ctx, minishell_backend_dir_t dir)
{
    (void)ctx;
    DIR *native_dir = dir_from_handle(dir);
    if (native_dir == NULL) return MINI_ERR_BAD_HANDLE;
    return closedir(native_dir) == 0 ? MINI_OK : result_from_errno(errno);
}

static uint64_t service_monotonic_us(void *ctx)
{
    (void)ctx;
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0u;
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}

static mini_result_t service_sleep_ms(void *ctx, uint32_t milliseconds)
{
    (void)ctx;
    struct timespec request = {
        .tv_sec = (time_t)(milliseconds / 1000u),
        .tv_nsec = (long)(milliseconds % 1000u) * 1000000L,
    };
    while (nanosleep(&request, &request) != 0) {
        if (errno != EINTR) return result_from_errno(errno);
    }
    return MINI_OK;
}

static mini_result_t service_utc_load(void *ctx, int64_t *out_seconds,
                                      uint32_t *out_nanoseconds)
{
    (void)ctx;
    if (out_seconds == NULL || out_nanoseconds == NULL) return MINI_ERR_INVALID;
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) return result_from_errno(errno);
    *out_seconds = (int64_t)ts.tv_sec;
    *out_nanoseconds = (uint32_t)ts.tv_nsec;
    return MINI_OK;
}

static mini_result_t location_path(char *out, size_t out_size)
{
    int written = snprintf(out, out_size, "%s/.state/location", s_state.root_dir);
    return (written < 0 || (size_t)written >= out_size)
               ? MINI_ERR_NAME_TOO_LONG : MINI_OK;
}

static mini_result_t service_default_location_load(void *ctx,
                                                   int32_t *out_latitude_e7,
                                                   int32_t *out_longitude_e7)
{
    (void)ctx;
    if (out_latitude_e7 == NULL || out_longitude_e7 == NULL) return MINI_ERR_INVALID;

    char path[PATH_MAX];
    mini_result_t result = location_path(path, sizeof(path));
    if (result != MINI_OK) return result;

    FILE *file = fopen(path, "r");
    if (file == NULL) return result_from_errno(errno);

    long latitude;
    long longitude;
    int matched = fscanf(file, "%ld %ld", &latitude, &longitude);
    int close_result = fclose(file);
    if (matched != 2 || close_result != 0 ||
        latitude < INT32_MIN || latitude > INT32_MAX ||
        longitude < INT32_MIN || longitude > INT32_MAX) {
        return MINI_ERR_IO;
    }

    *out_latitude_e7 = (int32_t)latitude;
    *out_longitude_e7 = (int32_t)longitude;
    return MINI_OK;
}

static mini_result_t service_default_location_store(void *ctx, int32_t latitude_e7,
                                                    int32_t longitude_e7)
{
    (void)ctx;
    char path[PATH_MAX];
    mini_result_t result = location_path(path, sizeof(path));
    if (result != MINI_OK) return result;

    FILE *file = fopen(path, "w");
    if (file == NULL) return result_from_errno(errno);
    if (fprintf(file, "%ld %ld\n", (long)latitude_e7, (long)longitude_e7) < 0 ||
        fflush(file) != 0) {
        int error = errno;
        fclose(file);
        return result_from_errno(error);
    }
    int fd = fileno(file);
    if (fd >= 0 && fsync(fd) != 0) {
        int error = errno;
        fclose(file);
        return result_from_errno(error);
    }
    return fclose(file) == 0 ? MINI_OK : result_from_errno(errno);
}

static mini_result_t service_default_location_clear(void *ctx)
{
    (void)ctx;
    char path[PATH_MAX];
    mini_result_t result = location_path(path, sizeof(path));
    if (result != MINI_OK) return result;
    if (unlink(path) == 0 || errno == ENOENT) return MINI_OK;
    return result_from_errno(errno);
}

static mini_result_t service_display_get_info(void *ctx, uint32_t *out_columns,
                                              uint32_t *out_rows)
{
    (void)ctx;
    if (out_columns == NULL || out_rows == NULL) return MINI_ERR_INVALID;

    struct winsize size;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0 &&
        size.ws_col != 0 && size.ws_row != 0) {
        *out_columns = size.ws_col;
        *out_rows = size.ws_row;
        return MINI_OK;
    }

    *out_columns = 80u;
    *out_rows = 24u;
    return MINI_OK;
}

static mini_result_t service_display_clear(void *ctx)
{
    (void)ctx;
    fputs("\033[2J\033[H", stdout);
    return ferror(stdout) ? MINI_ERR_IO : MINI_OK;
}

static mini_result_t service_display_clear_at(void *ctx, uint32_t row,
                                              uint32_t column, uint32_t rows,
                                              uint32_t columns)
{
    (void)ctx;
    for (uint32_t r = 0; r < rows; ++r) {
        if (fprintf(stdout, "\033[%u;%uH", (unsigned)(row + r + 1u),
                    (unsigned)(column + 1u)) < 0) {
            return MINI_ERR_IO;
        }
        for (uint32_t c = 0; c < columns; ++c) {
            if (fputc(' ', stdout) == EOF) return MINI_ERR_IO;
        }
    }
    return MINI_OK;
}

static mini_result_t service_display_write_at(void *ctx, uint32_t row,
                                              uint32_t column, const char *text,
                                              uint32_t byte_count)
{
    (void)ctx;
    if (fprintf(stdout, "\033[%u;%uH", (unsigned)(row + 1u),
                (unsigned)(column + 1u)) < 0) {
        return MINI_ERR_IO;
    }
    return fwrite(text, 1, byte_count, stdout) == byte_count ? MINI_OK : MINI_ERR_IO;
}

static mini_result_t service_display_present(void *ctx)
{
    (void)ctx;
    return fflush(stdout) == 0 ? MINI_OK : MINI_ERR_IO;
}

static mini_result_t submit_special(uint32_t key, uint32_t modifiers)
{
    mini_key_event_t event = {
        .struct_size = sizeof(event),
        .type = MINI_KEY_EVENT_SPECIAL,
        .codepoint = 0u,
        .key = key,
        .modifiers = modifiers,
    };
    return minishell_services_input_submit(&event);
}

static mini_result_t submit_char(uint32_t codepoint, uint32_t modifiers)
{
    mini_key_event_t event = {
        .struct_size = sizeof(event),
        .type = MINI_KEY_EVENT_CHAR,
        .codepoint = codepoint,
        .key = 0u,
        .modifiers = modifiers,
    };
    return minishell_services_input_submit(&event);
}

static size_t decode_utf8(const unsigned char *bytes, size_t available,
                          uint32_t *out_codepoint)
{
    if (available == 0u || out_codepoint == NULL) return 0u;
    unsigned char first = bytes[0];
    if (first < 0x80u) {
        *out_codepoint = first;
        return 1u;
    }

    size_t needed;
    uint32_t codepoint;
    if ((first & 0xE0u) == 0xC0u) {
        needed = 2u;
        codepoint = first & 0x1Fu;
    } else if ((first & 0xF0u) == 0xE0u) {
        needed = 3u;
        codepoint = first & 0x0Fu;
    } else if ((first & 0xF8u) == 0xF0u) {
        needed = 4u;
        codepoint = first & 0x07u;
    } else {
        return 0u;
    }
    if (available < needed) return 0u;

    for (size_t i = 1u; i < needed; ++i) {
        if ((bytes[i] & 0xC0u) != 0x80u) return 0u;
        codepoint = (codepoint << 6) | (uint32_t)(bytes[i] & 0x3Fu);
    }

    if ((needed == 2u && codepoint < 0x80u) ||
        (needed == 3u && codepoint < 0x800u) ||
        (needed == 4u && codepoint < 0x10000u) ||
        codepoint > 0x10FFFFu ||
        (codepoint >= 0xD800u && codepoint <= 0xDFFFu)) {
        return 0u;
    }

    *out_codepoint = codepoint;
    return needed;
}

static bool submit_input_bytes(const unsigned char *bytes, size_t count)
{
    bool submitted = false;
    size_t i = 0u;
    while (i < count) {
        unsigned char ch = bytes[i];

        if (ch == 0x1bu) {
            if (i + 2u < count && bytes[i + 1u] == '[') {
                unsigned char code = bytes[i + 2u];
                uint32_t key = 0u;
                if (code == 'A') key = MINI_KEY_UP;
                else if (code == 'B') key = MINI_KEY_DOWN;
                else if (code == 'C') key = MINI_KEY_RIGHT;
                else if (code == 'D') key = MINI_KEY_LEFT;
                else if (code == 'H') key = MINI_KEY_HOME;
                else if (code == 'F') key = MINI_KEY_END;
                if (key != 0u) {
                    (void)submit_special(key, 0u);
                    submitted = true;
                    i += 3u;
                    continue;
                }

                if (i + 3u < count && bytes[i + 3u] == '~') {
                    if (code == '2') key = MINI_KEY_INSERT;
                    else if (code == '3') key = MINI_KEY_DELETE;
                    else if (code == '5') key = MINI_KEY_PAGE_UP;
                    else if (code == '6') key = MINI_KEY_PAGE_DOWN;
                    if (key != 0u) {
                        (void)submit_special(key, 0u);
                        submitted = true;
                        i += 4u;
                        continue;
                    }
                }
            }
            (void)submit_special(MINI_KEY_ESCAPE, 0u);
            submitted = true;
            ++i;
            continue;
        }

        if (ch == '\r' || ch == '\n') {
            (void)submit_special(MINI_KEY_ENTER, 0u);
            submitted = true;
            ++i;
            continue;
        }
        if (ch == '\t') {
            (void)submit_special(MINI_KEY_TAB, 0u);
            submitted = true;
            ++i;
            continue;
        }
        if (ch == 0x08u || ch == 0x7fu) {
            (void)submit_special(MINI_KEY_BACKSPACE, 0u);
            submitted = true;
            ++i;
            continue;
        }
        if (ch >= 1u && ch <= 26u) {
            (void)submit_char((uint32_t)('a' + ch - 1u), MINI_MOD_CTRL);
            submitted = true;
            ++i;
            continue;
        }
        if (ch >= 0x20u && ch < 0x7fu) {
            (void)submit_char(ch, 0u);
            submitted = true;
            ++i;
            continue;
        }
        if (ch >= 0x80u) {
            uint32_t codepoint = 0u;
            size_t used = decode_utf8(&bytes[i], count - i, &codepoint);
            if (used != 0u) {
                (void)submit_char(codepoint, 0u);
                submitted = true;
                i += used;
                continue;
            }
        }
        ++i;
    }
    return submitted;
}

static void service_input_lock(void *ctx) { (void)ctx; }
static void service_input_unlock(void *ctx) { (void)ctx; }
static void service_input_wake(void *ctx) { (void)ctx; }

static mini_result_t service_input_wait(void *ctx, uint32_t timeout_ms)
{
    (void)ctx;
    struct pollfd descriptor = {
        .fd = STDIN_FILENO,
        .events = POLLIN,
        .revents = 0,
    };

    int timeout;
    if (timeout_ms == MINI_WAIT_FOREVER) timeout = -1;
    else if (timeout_ms > (uint32_t)INT_MAX) timeout = INT_MAX;
    else timeout = (int)timeout_ms;

    int ready;
    do {
        ready = poll(&descriptor, 1, timeout);
    } while (ready < 0 && errno == EINTR);

    if (ready < 0) return result_from_errno(errno);
    if (ready == 0) return timeout_ms == MINI_WAIT_NONE ? MINI_ERR_NOT_READY : MINI_ERR_TIMEOUT;
    if ((descriptor.revents & (POLLERR | POLLNVAL)) != 0) return MINI_ERR_IO;
    if ((descriptor.revents & (POLLIN | POLLHUP)) == 0) return MINI_ERR_NOT_READY;

    unsigned char buffer[INPUT_READ_MAX];
    ssize_t count;
    do {
        count = read(STDIN_FILENO, buffer, sizeof(buffer));
    } while (count < 0 && errno == EINTR);
    if (count < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return MINI_ERR_NOT_READY;
        return result_from_errno(errno);
    }
    if (count == 0) return MINI_ERR_NOT_READY;

    return submit_input_bytes(buffer, (size_t)count) ? MINI_OK : MINI_ERR_NOT_READY;
}

static void service_input_flush(void *ctx)
{
    (void)ctx;
    if (isatty(STDIN_FILENO)) {
        (void)tcflush(STDIN_FILENO, TCIFLUSH);
    }
}

static int terminal_app_begin(void)
{
    if (!isatty(STDIN_FILENO)) return 0;
    if (tcgetattr(STDIN_FILENO, &s_state.saved_termios) != 0) return -errno;

    struct termios mode = s_state.saved_termios;
    mode.c_lflag &= (tcflag_t)~(ICANON | ECHO);
    mode.c_iflag &= (tcflag_t)~(IXON | IXOFF);
    mode.c_cc[VMIN] = 0;
    mode.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSANOW, &mode) != 0) return -errno;
    s_state.terminal_mode_active = true;
    return 0;
}

static void terminal_app_end(void)
{
    if (!s_state.terminal_mode_active) return;
    (void)tcsetattr(STDIN_FILENO, TCSANOW, &s_state.saved_termios);
    s_state.terminal_mode_active = false;
}

static void configure_services_port(void)
{
    memset(&s_services_port, 0, sizeof(s_services_port));
    s_services_port.ctx = &s_state;

    s_services_port.system_write = service_system_write;

    s_services_port.memory_alloc = service_memory_alloc;
    s_services_port.memory_realloc = service_memory_realloc;
    s_services_port.memory_free = service_memory_free;
    s_services_port.memory_get_info = service_memory_get_info;

    s_services_port.fs_open = service_fs_open;
    s_services_port.fs_close = service_fs_close;
    s_services_port.fs_read = service_fs_read;
    s_services_port.fs_write = service_fs_write;
    s_services_port.fs_seek = service_fs_seek;
    s_services_port.fs_sync = service_fs_sync;
    s_services_port.fs_stat = service_fs_stat;
    s_services_port.fs_rename = service_fs_rename;
    s_services_port.fs_remove_file = service_fs_remove_file;
    s_services_port.fs_mkdir = service_fs_mkdir;
    s_services_port.fs_rmdir = service_fs_rmdir;
    s_services_port.fs_dir_open = service_fs_dir_open;
    s_services_port.fs_dir_read = service_fs_dir_read;
    s_services_port.fs_dir_close = service_fs_dir_close;

    s_services_port.monotonic_us = service_monotonic_us;
    s_services_port.sleep_ms = service_sleep_ms;
    s_services_port.time_location_capabilities =
        MINI_TIMELOC_CAP_UTC |
        MINI_TIMELOC_CAP_LOCATION |
        MINI_TIMELOC_CAP_DEFAULT_LOCATION |
        MINI_TIMELOC_CAP_SET_DEFAULT_LOCATION;
    s_services_port.utc_load = service_utc_load;
    s_services_port.default_location_load = service_default_location_load;
    s_services_port.default_location_store = service_default_location_store;
    s_services_port.default_location_clear = service_default_location_clear;

    s_services_port.display_capabilities = MINI_DISPLAY_CAP_TEXT;
    s_services_port.display_text_get_info = service_display_get_info;
    s_services_port.display_text_clear = service_display_clear;
    s_services_port.display_text_clear_at = service_display_clear_at;
    s_services_port.display_text_write_at = service_display_write_at;
    s_services_port.display_present = service_display_present;

    s_services_port.input_capabilities = MINI_INPUT_CAP_KEY;
    s_services_port.input_lock = service_input_lock;
    s_services_port.input_unlock = service_input_unlock;
    s_services_port.input_wait = service_input_wait;
    s_services_port.input_wake = service_input_wake;
    s_services_port.input_flush = service_input_flush;
}

int minishell_platform_init(void)
{
    memset(&s_state, 0, sizeof(s_state));
    int ret = build_default_paths();
    if (ret != 0) return ret;
    ret = prepare_logical_root();
    if (ret != 0) return ret;
    configure_services_port();
    return 0;
}

void minishell_platform_shutdown(void)
{
    terminal_app_end();
}

const minishell_services_port_t *minishell_platform_services_port(void)
{
    return &s_services_port;
}

static int has_so_suffix(const char *name)
{
    size_t length = strlen(name);
    return length > 3 && strcmp(name + length - 3, ".so") == 0;
}

static int compare_names(const void *a, const void *b)
{
    const char *const *left = a;
    const char *const *right = b;
    return strcmp(*left, *right);
}

int minishell_platform_apps_list(minishell_app_emit_fn emit, void *ctx)
{
    DIR *dir = opendir(app_dir());
    if (dir == NULL) return -errno;

    char **names = NULL;
    size_t count = 0;
    size_t capacity = 0;
    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.' || !has_so_suffix(entry->d_name)) continue;

        size_t name_length = strlen(entry->d_name) - 3;
        if (name_length == 0 || name_length >= APP_NAME_MAX) continue;

        if (count == capacity) {
            size_t new_capacity = capacity == 0 ? 8 : capacity * 2;
            char **new_names = realloc(names, new_capacity * sizeof(*new_names));
            if (new_names == NULL) {
                closedir(dir);
                for (size_t i = 0; i < count; ++i) free(names[i]);
                free(names);
                return -ENOMEM;
            }
            names = new_names;
            capacity = new_capacity;
        }

        names[count] = strndup(entry->d_name, name_length);
        if (names[count] == NULL) {
            closedir(dir);
            for (size_t i = 0; i < count; ++i) free(names[i]);
            free(names);
            return -ENOMEM;
        }
        ++count;
    }

    closedir(dir);
    qsort(names, count, sizeof(*names), compare_names);
    for (size_t i = 0; i < count; ++i) {
        emit(names[i], ctx);
        free(names[i]);
    }
    free(names);
    return 0;
}

int minishell_platform_app_run(const char *name, int argc, char **argv)
{
    if (name == NULL || name[0] == '\0' || strchr(name, '/') != NULL) return -EINVAL;

    char path[PATH_MAX];
    int written = snprintf(path, sizeof(path), "%s/%s.so", app_dir(), name);
    if (written < 0 || (size_t)written >= sizeof(path)) return -ENAMETOOLONG;

    void *handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (handle == NULL) {
        if (access(path, F_OK) != 0) return -ENOENT;
        fprintf(stderr, "app: dlopen %s: %s\n", path, dlerror());
        return -ENOEXEC;
    }

    dlerror();
    int (*entry)(int, char **) = NULL;
    *(void **)(&entry) = dlsym(handle, "main");
    const char *error = dlerror();
    if (error != NULL) {
        fprintf(stderr, "app: %s has no main: %s\n", name, error);
        dlclose(handle);
        return -ENOEXEC;
    }

    int terminal_result = terminal_app_begin();
    if (terminal_result != 0) {
        fprintf(stderr, "app: terminal handoff failed (%d)\n", terminal_result);
        dlclose(handle);
        return terminal_result;
    }

    int result = entry(argc, argv);

    terminal_app_end();
    if (dlclose(handle) != 0) {
        fprintf(stderr, "app: dlclose %s: %s\n", name, dlerror());
        if (result == 0) result = -EIO;
    }
    return result;
}

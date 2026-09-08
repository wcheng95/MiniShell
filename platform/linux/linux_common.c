#define _GNU_SOURCE

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "linux_internal.h"

typedef struct {
    char app_dir[PATH_MAX];
    char root_dir[PATH_MAX];
} linux_paths_t;

static linux_paths_t s_paths;

mini_result_t linux_result_from_errno(int error)
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
    if (mkdir(path, 0777) == 0) return 0;
    if (errno != EEXIST) return -errno;

    struct stat st;
    if (stat(path, &st) != 0) return -errno;
    return S_ISDIR(st.st_mode) ? 0 : -ENOTDIR;
}

static int mkdir_p(const char *path)
{
    char temp[PATH_MAX];
    size_t length = strlen(path);
    if (length == 0u || length >= sizeof(temp)) return -ENAMETOOLONG;

    memcpy(temp, path, length + 1u);
    for (char *p = temp + 1; *p != '\0'; ++p) {
        if (*p != '/') continue;
        *p = '\0';
        int result = mkdir_one(temp);
        *p = '/';
        if (result != 0) return result;
    }
    return mkdir_one(temp);
}

int linux_paths_init(void)
{
    memset(&s_paths, 0, sizeof(s_paths));

    char executable[PATH_MAX];
    ssize_t length = readlink("/proc/self/exe", executable, sizeof(executable) - 1u);
    if (length < 0 || (size_t)length >= sizeof(executable) - 1u) return -errno;
    executable[length] = '\0';

    char *slash = strrchr(executable, '/');
    if (slash == NULL) return -EINVAL;
    *slash = '\0';

    int written = snprintf(s_paths.app_dir, sizeof(s_paths.app_dir),
                           "%s/runtime/apps", executable);
    if (written < 0 || (size_t)written >= sizeof(s_paths.app_dir)) {
        return -ENAMETOOLONG;
    }

    const char *root_override = getenv("MINISHELL_ROOT");
    if (root_override != NULL && root_override[0] != '\0') {
        written = snprintf(s_paths.root_dir, sizeof(s_paths.root_dir), "%s", root_override);
    } else {
        const char *xdg = getenv("XDG_DATA_HOME");
        if (xdg != NULL && xdg[0] != '\0') {
            written = snprintf(s_paths.root_dir, sizeof(s_paths.root_dir),
                               "%s/minishell/fs", xdg);
        } else {
            const char *home = getenv("HOME");
            if (home == NULL || home[0] == '\0') return -ENOENT;
            written = snprintf(s_paths.root_dir, sizeof(s_paths.root_dir),
                               "%s/.local/share/minishell/fs", home);
        }
    }

    if (written < 0 || (size_t)written >= sizeof(s_paths.root_dir)) {
        return -ENAMETOOLONG;
    }
    return 0;
}

int linux_prepare_logical_root(void)
{
    int result = mkdir_p(s_paths.root_dir);
    if (result != 0) return result;

    const char *children[] = {"sd", "flash", ".state"};
    for (size_t i = 0; i < sizeof(children) / sizeof(children[0]); ++i) {
        char path[PATH_MAX];
        int written = snprintf(path, sizeof(path), "%s/%s",
                               s_paths.root_dir, children[i]);
        if (written < 0 || (size_t)written >= sizeof(path)) return -ENAMETOOLONG;
        result = mkdir_p(path);
        if (result != 0) return result;
    }
    return 0;
}

const char *linux_root_dir(void)
{
    return s_paths.root_dir;
}

const char *linux_app_dir(void)
{
    const char *override = getenv("MINISHELL_APP_DIR");
    return override != NULL && override[0] != '\0' ? override : s_paths.app_dir;
}

mini_result_t linux_host_path(const char *logical, char *out, size_t out_size)
{
    if (logical == NULL || logical[0] != '/' || out == NULL || out_size == 0u) {
        return MINI_ERR_INVALID;
    }

    int written;
    if (strcmp(logical, "/") == 0) {
        written = snprintf(out, out_size, "%s", s_paths.root_dir);
    } else {
        written = snprintf(out, out_size, "%s%s", s_paths.root_dir, logical);
    }
    if (written < 0 || (size_t)written >= out_size) return MINI_ERR_NAME_TOO_LONG;
    return MINI_OK;
}

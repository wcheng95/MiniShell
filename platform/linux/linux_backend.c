#define _GNU_SOURCE

#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "platform_backend.h"

#define APP_NAME_MAX 128

static char s_default_app_dir[PATH_MAX];

static const char *app_dir(void)
{
    const char *override = getenv("MINISHELL_APP_DIR");
    return (override != NULL && override[0] != '\0') ? override : s_default_app_dir;
}

int minishell_platform_init(void)
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

    int written = snprintf(s_default_app_dir,
                           sizeof(s_default_app_dir),
                           "%s/runtime/apps",
                           executable);
    if (written < 0 || (size_t)written >= sizeof(s_default_app_dir)) {
        return -ENAMETOOLONG;
    }

    return 0;
}

void minishell_platform_shutdown(void)
{
}

void minishell_platform_write(const char *text)
{
    fputs(text, stdout);
    fflush(stdout);
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
    if (dir == NULL) {
        return -errno;
    }

    char **names = NULL;
    size_t count = 0;
    size_t capacity = 0;
    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.' || !has_so_suffix(entry->d_name)) {
            continue;
        }

        size_t name_length = strlen(entry->d_name) - 3;
        if (name_length == 0 || name_length >= APP_NAME_MAX) {
            continue;
        }

        if (count == capacity) {
            size_t new_capacity = capacity == 0 ? 8 : capacity * 2;
            char **new_names = realloc(names, new_capacity * sizeof(*new_names));
            if (new_names == NULL) {
                closedir(dir);
                for (size_t i = 0; i < count; ++i) {
                    free(names[i]);
                }
                free(names);
                return -ENOMEM;
            }
            names = new_names;
            capacity = new_capacity;
        }

        names[count] = strndup(entry->d_name, name_length);
        if (names[count] == NULL) {
            closedir(dir);
            for (size_t i = 0; i < count; ++i) {
                free(names[i]);
            }
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
    if (name == NULL || name[0] == '\0' || strchr(name, '/') != NULL) {
        return -EINVAL;
    }

    char path[PATH_MAX];
    int written = snprintf(path, sizeof(path), "%s/%s.so", app_dir(), name);
    if (written < 0 || (size_t)written >= sizeof(path)) {
        return -ENAMETOOLONG;
    }

    void *handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (handle == NULL) {
        if (access(path, F_OK) != 0) {
            return -ENOENT;
        }
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

    int result = entry(argc, argv);

    if (dlclose(handle) != 0) {
        fprintf(stderr, "app: dlclose %s: %s\n", name, dlerror());
        if (result == 0) {
            result = -EIO;
        }
    }

    return result;
}

#define _GNU_SOURCE

#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "linux_internal.h"

#define APP_NAME_MAX 128u

static minishell_platform_result_t platform_result_from_errno(int error)
{
    switch (error) {
        case 0: return MINISHELL_PLATFORM_OK;
        case EINVAL: return MINISHELL_PLATFORM_ERR_INVALID;
        case ENOENT: return MINISHELL_PLATFORM_ERR_NOT_FOUND;
        case ENOMEM: return MINISHELL_PLATFORM_ERR_NO_MEMORY;
        case ENAMETOOLONG: return MINISHELL_PLATFORM_ERR_NAME_TOO_LONG;
        default: return MINISHELL_PLATFORM_ERR_IO;
    }
}

static int has_so_suffix(const char *name)
{
    size_t length = strlen(name);
    return length > 3u && strcmp(name + length - 3u, ".so") == 0;
}

static int compare_names(const void *a, const void *b)
{
    const char *const *left = a;
    const char *const *right = b;
    return strcmp(*left, *right);
}

minishell_platform_result_t linux_loader_apps_list(minishell_app_emit_fn emit,
                                                   void *ctx)
{
    if (emit == NULL) return MINISHELL_PLATFORM_ERR_INVALID;

    DIR *dir = opendir(linux_app_dir());
    if (dir == NULL) return platform_result_from_errno(errno);

    char **names = NULL;
    size_t count = 0u;
    size_t capacity = 0u;
    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.' || !has_so_suffix(entry->d_name)) continue;

        size_t name_length = strlen(entry->d_name) - 3u;
        if (name_length == 0u || name_length >= APP_NAME_MAX) continue;

        if (count == capacity) {
            size_t new_capacity = capacity == 0u ? 8u : capacity * 2u;
            char **new_names = realloc(names, new_capacity * sizeof(*new_names));
            if (new_names == NULL) {
                (void)closedir(dir);
                for (size_t i = 0u; i < count; ++i) free(names[i]);
                free(names);
                return MINISHELL_PLATFORM_ERR_NO_MEMORY;
            }
            names = new_names;
            capacity = new_capacity;
        }

        names[count] = strndup(entry->d_name, name_length);
        if (names[count] == NULL) {
            (void)closedir(dir);
            for (size_t i = 0u; i < count; ++i) free(names[i]);
            free(names);
            return MINISHELL_PLATFORM_ERR_NO_MEMORY;
        }
        ++count;
    }

    if (closedir(dir) != 0) {
        for (size_t i = 0u; i < count; ++i) free(names[i]);
        free(names);
        return MINISHELL_PLATFORM_ERR_IO;
    }

    qsort(names, count, sizeof(*names), compare_names);
    for (size_t i = 0u; i < count; ++i) {
        emit(names[i], ctx);
        free(names[i]);
    }
    free(names);
    return MINISHELL_PLATFORM_OK;
}

minishell_platform_result_t linux_loader_app_run(const char *name,
                                                 int argc,
                                                 char **argv,
                                                 int *out_app_result)
{
    if (out_app_result == NULL) return MINISHELL_PLATFORM_ERR_INVALID;
    *out_app_result = 0;

    if (name == NULL || name[0] == '\0' || strchr(name, '/') != NULL) {
        return MINISHELL_PLATFORM_ERR_INVALID;
    }

    char path[PATH_MAX];
    int written = snprintf(path, sizeof(path), "%s/%s.so", linux_app_dir(), name);
    if (written < 0 || (size_t)written >= sizeof(path)) {
        return MINISHELL_PLATFORM_ERR_NAME_TOO_LONG;
    }

    void *handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (handle == NULL) {
        if (access(path, F_OK) != 0) return platform_result_from_errno(errno);
        fprintf(stderr, "app: dlopen %s: %s\n", path, dlerror());
        return MINISHELL_PLATFORM_ERR_LOAD;
    }

    dlerror();
    int (*entry)(int, char **) = NULL;
    *(void **)(&entry) = dlsym(handle, "main");
    const char *error = dlerror();
    if (error != NULL) {
        fprintf(stderr, "app: %s has no main: %s\n", name, error);
        (void)dlclose(handle);
        return MINISHELL_PLATFORM_ERR_LOAD;
    }

    int terminal_result = linux_terminal_app_begin();
    if (terminal_result != 0) {
        fprintf(stderr, "app: terminal handoff failed (%d)\n", terminal_result);
        (void)dlclose(handle);
        return MINISHELL_PLATFORM_ERR_IO;
    }

    *out_app_result = entry(argc, argv);

    linux_terminal_app_end();
    if (dlclose(handle) != 0) {
        fprintf(stderr, "app: dlclose %s: %s\n", name, dlerror());
        return MINISHELL_PLATFORM_ERR_IO;
    }
    return MINISHELL_PLATFORM_OK;
}

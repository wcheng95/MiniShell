#include "adv_elf_loader.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "adv_internal.h"
#include "esp_elf.h"
#include "minishell/api.h"

#define ADV_ELF_APP_NAME_MAX 127u

static const struct esp_elfsym s_minishell_symbols[] = {
    ESP_ELFSYM_EXPORT(mini_api_get),
    ESP_ELFSYM_END,
};

static bool s_symbols_registered;

static bool valid_root(const char *root)
{
    return root != NULL &&
           (strcmp(root, "/flash") == 0 || strcmp(root, "/sd") == 0);
}

static bool valid_name(const char *name)
{
    return name != NULL && name[0] != '\0' && strchr(name, '/') == NULL &&
           strlen(name) <= ADV_ELF_APP_NAME_MAX;
}

static bool build_path(char *path, size_t path_size,
                       const char *root, const char *name)
{
    if (path == NULL || path_size == 0u || !valid_root(root) || !valid_name(name)) {
        return false;
    }

    int written = snprintf(path, path_size, "%s/%s.elf", root, name);
    return written >= 0 && (size_t)written < path_size;
}

static bool filename_to_name(const char *filename,
                             char out_name[ADV_ELF_APP_NAME_MAX + 1u])
{
    size_t length;
    size_t name_length;

    if (filename == NULL || out_name == NULL) return false;
    length = strlen(filename);
    if (length <= 4u || strcmp(filename + length - 4u, ".elf") != 0) return false;

    name_length = length - 4u;
    if (name_length == 0u || name_length > ADV_ELF_APP_NAME_MAX) return false;
    memcpy(out_name, filename, name_length);
    out_name[name_length] = '\0';
    return valid_name(out_name);
}

static minishell_platform_result_t ensure_minishell_symbols(void)
{
    int result;

    if (s_symbols_registered) return MINISHELL_PLATFORM_OK;

    result = esp_elf_register_symbol(s_minishell_symbols);
    if (result != 0 && result != -EEXIST) {
        return MINISHELL_PLATFORM_ERR_LOAD;
    }

    s_symbols_registered = true;
    return MINISHELL_PLATFORM_OK;
}

static minishell_platform_result_t read_file(const char *path,
                                             uint8_t **out_data,
                                             size_t *out_size)
{
    FILE *file;
    long file_size;
    uint8_t *data;

    if (path == NULL || out_data == NULL || out_size == NULL) {
        return MINISHELL_PLATFORM_ERR_INVALID;
    }
    *out_data = NULL;
    *out_size = 0u;

    file = fopen(path, "rb");
    if (file == NULL) {
        return errno == ENOENT ? MINISHELL_PLATFORM_ERR_NOT_FOUND
                               : MINISHELL_PLATFORM_ERR_IO;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        (void)fclose(file);
        return MINISHELL_PLATFORM_ERR_IO;
    }
    file_size = ftell(file);
    if (file_size <= 0 || (uint64_t)file_size > (uint64_t)SIZE_MAX) {
        (void)fclose(file);
        return MINISHELL_PLATFORM_ERR_LOAD;
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
        (void)fclose(file);
        return MINISHELL_PLATFORM_ERR_IO;
    }

    data = malloc((size_t)file_size);
    if (data == NULL) {
        (void)fclose(file);
        return MINISHELL_PLATFORM_ERR_NO_MEMORY;
    }

    if (fread(data, 1u, (size_t)file_size, file) != (size_t)file_size) {
        free(data);
        (void)fclose(file);
        return MINISHELL_PLATFORM_ERR_IO;
    }
    if (fclose(file) != 0) {
        free(data);
        return MINISHELL_PLATFORM_ERR_IO;
    }

    *out_data = data;
    *out_size = (size_t)file_size;
    return MINISHELL_PLATFORM_OK;
}

bool adv_elf_loader_app_exists(const char *root, const char *name)
{
    char path[PATH_MAX];
    struct stat st;

    if (!build_path(path, sizeof(path), root, name)) return false;
    if (stat(path, &st) != 0) return false;
    return S_ISREG(st.st_mode);
}

minishell_platform_result_t adv_elf_loader_apps_list(const char *root,
                                                     minishell_app_emit_fn emit,
                                                     void *ctx)
{
    DIR *dir;
    struct dirent *entry;

    if (!valid_root(root) || emit == NULL) return MINISHELL_PLATFORM_ERR_INVALID;

    dir = opendir(root);
    if (dir == NULL) {
        return errno == ENOENT ? MINISHELL_PLATFORM_OK : MINISHELL_PLATFORM_ERR_IO;
    }

    while ((entry = readdir(dir)) != NULL) {
        char name[ADV_ELF_APP_NAME_MAX + 1u];
        char path[PATH_MAX];
        struct stat st;

        if (entry->d_name[0] == '.') continue;
        if (!filename_to_name(entry->d_name, name)) continue;
        if (!build_path(path, sizeof(path), root, name)) continue;
        if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) continue;
        emit(name, ctx);
    }

    if (closedir(dir) != 0) return MINISHELL_PLATFORM_ERR_IO;
    return MINISHELL_PLATFORM_OK;
}

minishell_platform_result_t adv_elf_loader_app_run(const char *root,
                                                   const char *name,
                                                   int argc,
                                                   char **argv,
                                                   int *out_app_result)
{
    char path[PATH_MAX];
    char log_line[PATH_MAX + 32u];
    uint8_t *data = NULL;
    size_t data_size = 0u;
    esp_elf_t elf;
    bool elf_initialized = false;
    minishell_platform_result_t result;

    if (out_app_result == NULL || !build_path(path, sizeof(path), root, name)) {
        return MINISHELL_PLATFORM_ERR_INVALID;
    }
    *out_app_result = 0;

    result = ensure_minishell_symbols();
    if (result != MINISHELL_PLATFORM_OK) return result;

    result = read_file(path, &data, &data_size);
    if (result != MINISHELL_PLATFORM_OK) return result;
    (void)data_size;

    int written = snprintf(log_line, sizeof(log_line), "ADV ELF: loading %s\n", path);
    if (written > 0 && (size_t)written < sizeof(log_line)) {
        adv_console_debug_write(log_line);
    }

    if (esp_elf_init(&elf) != 0) {
        result = MINISHELL_PLATFORM_ERR_LOAD;
        goto cleanup;
    }
    elf_initialized = true;

    if (esp_elf_relocate(&elf, data) != 0) {
        result = MINISHELL_PLATFORM_ERR_LOAD;
        goto cleanup;
    }

    if (esp_elf_request(&elf, 0, argc, argv) != 0) {
        result = MINISHELL_PLATFORM_ERR_LOAD;
        goto cleanup;
    }

    result = MINISHELL_PLATFORM_OK;

cleanup:
    if (elf_initialized) esp_elf_deinit(&elf);
    free(data);
    return result;
}

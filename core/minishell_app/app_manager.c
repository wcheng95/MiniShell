#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_elf.h"

#include "minishell/api.h"
#include "minishell_app.h"
#include "minishell_services.h"

#define APP_DIR "/sd/apps"
#define APP_NAME_MAX 96

static bool s_initialized;

static esp_elf_symbol_table_t s_minishell_symbols[] = {
    ESP_ELFSYM_EXPORT(mini_api_get),
    ESP_ELFSYM_END,
};

static bool has_elf_suffix(const char *name)
{
    size_t len = strlen(name);
    return len >= 4 && strcmp(name + len - 4, ".elf") == 0;
}

static int make_filename(const char *command, char *filename, size_t size)
{
    if (command == NULL || command[0] == '\0' || strchr(command, '/') != NULL) return -EINVAL;
    int written;
    if (has_elf_suffix(command)) written = snprintf(filename, size, "%s", command);
    else written = snprintf(filename, size, "%s.elf", command);
    if (written < 0 || (size_t)written >= size) return -ENAMETOOLONG;
    return 0;
}

int minishell_app_init(void)
{
    if (s_initialized) return 0;
    int ret = esp_elf_register_symbol(s_minishell_symbols);
    if (ret != 0) return ret;
    s_initialized = true;
    return 0;
}

int minishell_app_run(const char *command, int argc, char **argv)
{
    char filename[APP_NAME_MAX];
    char fullpath[sizeof(APP_DIR) + APP_NAME_MAX + 2];
    int ret = make_filename(command, filename, sizeof(filename));
    if (ret != 0) return ret;
    int written = snprintf(fullpath, sizeof(fullpath), "%s/%s", APP_DIR, filename);
    if (written < 0 || (size_t)written >= sizeof(fullpath)) return -ENAMETOOLONG;
    struct stat st;
    if (stat(fullpath, &st) != 0) return -errno;
    elf_file_t file = {0};
    esp_elf_t elf;
    bool elf_initialized = false;
    printf("app: loading %s\n", fullpath);
    ret = esp_elf_open(&file, filename);
    if (ret < 0) return ret;
    ret = esp_elf_init(&elf);
    if (ret < 0) goto cleanup;
    elf_initialized = true;
    ret = esp_elf_relocate(&elf, file.payload);
    if (ret < 0) goto cleanup;
    minishell_services_app_begin();
    ret = esp_elf_request(&elf, 0, argc, argv);
    minishell_services_app_end();
cleanup:
    if (elf_initialized) esp_elf_deinit(&elf);
    esp_elf_close(&file);
    return ret;
}

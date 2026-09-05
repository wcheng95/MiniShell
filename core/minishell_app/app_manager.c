#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_elf.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "minishell/api.h"
#include "minishell_app.h"
#include "minishell_services.h"

#define APP_DIR "/sd/apps"
#define APP_NAME_MAX 96
#define APP_TASK_STACK_SIZE 8192u
#define APP_TASK_PRIORITY (tskIDLE_PRIORITY + 1u)

typedef struct {
    esp_elf_t *elf;
    int argc;
    char **argv;
    SemaphoreHandle_t done;
    volatile int result;
} app_execution_t;

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

static void app_task(void *argument)
{
    app_execution_t *execution = (app_execution_t *)argument;

    minishell_services_app_begin();
    execution->result = esp_elf_request(execution->elf, 0,
                                        execution->argc, execution->argv);
    minishell_services_app_end();

    xSemaphoreGive(execution->done);
    vTaskDelete(NULL);
}

static int run_relocated_app(esp_elf_t *elf, int argc, char **argv)
{
    SemaphoreHandle_t done = xSemaphoreCreateBinary();
    if (done == NULL) return -ENOMEM;

    app_execution_t execution = {
        .elf = elf,
        .argc = argc,
        .argv = argv,
        .done = done,
        .result = -EIO,
    };

    BaseType_t created = xTaskCreate(app_task,
                                     "minishell-app",
                                     APP_TASK_STACK_SIZE,
                                     &execution,
                                     APP_TASK_PRIORITY,
                                     NULL);
    if (created != pdPASS) {
        vSemaphoreDelete(done);
        return -ENOMEM;
    }

    if (xSemaphoreTake(done, portMAX_DELAY) != pdTRUE) {
        vSemaphoreDelete(done);
        return -EIO;
    }

    int result = execution.result;
    vSemaphoreDelete(done);
    return result;
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
    if (ret < 0) {
        printf("app: elf_open failed (%d)\n", ret);
        return ret;
    }

    ret = esp_elf_init(&elf);
    if (ret < 0) {
        printf("app: elf_init failed (%d)\n", ret);
        goto cleanup;
    }
    elf_initialized = true;

    ret = esp_elf_relocate(&elf, file.payload);
    if (ret < 0) {
        printf("app: elf_relocate failed (%d)\n", ret);
        goto cleanup;
    }

    ret = run_relocated_app(&elf, argc, argv);
    if (ret < 0) {
        printf("app: elf_request failed (%d)\n", ret);
    }

cleanup:
    if (elf_initialized) esp_elf_deinit(&elf);
    esp_elf_close(&file);
    return ret;
}

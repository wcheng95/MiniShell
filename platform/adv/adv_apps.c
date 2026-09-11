#include <stddef.h>
#include <string.h>

#include "adv_elf_loader.h"
#include "platform_backend.h"

#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

typedef int (*adv_app_entry_fn)(int argc, char **argv);

typedef struct {
    const char *name;
    adv_app_entry_fn entry;
} adv_app_entry_t;

extern int minishell_app_hello_main(int argc, char **argv);
extern int minishell_app_a2_probe_main(int argc, char **argv);
extern int minishell_app_a3_probe_main(int argc, char **argv);
extern int minishell_app_date_main(int argc, char **argv);
extern int minishell_app_free_main(int argc, char **argv);
extern int minishell_app_ls_main(int argc, char **argv);
extern int minishell_app_cat_main(int argc, char **argv);
extern int minishell_app_cp_main(int argc, char **argv);
extern int minishell_app_mv_main(int argc, char **argv);
extern int minishell_app_rm_main(int argc, char **argv);
extern int minishell_app_mkdir_main(int argc, char **argv);
extern int minishell_app_rmdir_main(int argc, char **argv);
extern int minishell_app_nano_main(int argc, char **argv);
extern int minishell_app_ft8_main(int argc, char **argv);
extern int minishell_app_usbmsc_main(int argc, char **argv);

static const adv_app_entry_t s_apps[] = {
    {"hello", minishell_app_hello_main},
    {"probe", minishell_app_a2_probe_main},
    {"a3probe", minishell_app_a3_probe_main},
    {"date", minishell_app_date_main},
    {"free", minishell_app_free_main},
    {"ls", minishell_app_ls_main},
    {"cat", minishell_app_cat_main},
    {"cp", minishell_app_cp_main},
    {"mv", minishell_app_mv_main},
    {"rm", minishell_app_rm_main},
    {"mkdir", minishell_app_mkdir_main},
    {"rmdir", minishell_app_rmdir_main},
    {"nano", minishell_app_nano_main},
    {"ft8", minishell_app_ft8_main},
    {"usbmsc", minishell_app_usbmsc_main},
};

static int valid_app_name(const char *name)
{
    return name != NULL && name[0] != '\0' && strchr(name, '/') == NULL;
}

static const adv_app_entry_t *find_static_app(const char *name)
{
    if (name == NULL) return NULL;
    for (size_t i = 0u; i < sizeof(s_apps) / sizeof(s_apps[0]); ++i) {
        if (strcmp(name, s_apps[i].name) == 0) return &s_apps[i];
    }
    return NULL;
}

#ifdef ESP_PLATFORM

#define ADV_APP_STACK_BYTES (16u * 1024u)
#define ADV_APP_TASK_PRIORITY (tskIDLE_PRIORITY + 1u)
#define ADV_APP_TASK_CORE 0

typedef struct {
    adv_app_entry_fn entry;
    const char *elf_root;
    const char *elf_name;
    int argc;
    char **argv;
    int result;
    minishell_platform_result_t launch_result;
    TaskHandle_t caller;
} adv_app_task_context_t;

static void adv_app_task(void *arg)
{
    adv_app_task_context_t *context = (adv_app_task_context_t *)arg;

    if (context->entry != NULL) {
        context->result = context->entry(context->argc, context->argv);
        context->launch_result = MINISHELL_PLATFORM_OK;
    } else {
        context->launch_result = adv_elf_loader_app_run(
            context->elf_root, context->elf_name,
            context->argc, context->argv, &context->result);
    }

    xTaskNotifyGive(context->caller);
    vTaskDelete(NULL);
}

static minishell_platform_result_t run_task(adv_app_entry_fn entry,
                                            const char *elf_root,
                                            const char *elf_name,
                                            int argc,
                                            char **argv,
                                            int *out_app_result)
{
    adv_app_task_context_t context = {
        .entry = entry,
        .elf_root = elf_root,
        .elf_name = elf_name,
        .argc = argc,
        .argv = argv,
        .result = 0,
        .launch_result = MINISHELL_PLATFORM_OK,
        .caller = xTaskGetCurrentTaskHandle(),
    };

    BaseType_t created = xTaskCreatePinnedToCore(
        adv_app_task,
        "mini-app",
        ADV_APP_STACK_BYTES,
        &context,
        ADV_APP_TASK_PRIORITY,
        NULL,
        ADV_APP_TASK_CORE);
    if (created != pdPASS) return MINISHELL_PLATFORM_ERR_NO_MEMORY;

    (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (context.launch_result == MINISHELL_PLATFORM_OK) {
        *out_app_result = context.result;
    }
    return context.launch_result;
}

static minishell_platform_result_t run_entry(adv_app_entry_fn entry,
                                             int argc,
                                             char **argv,
                                             int *out_app_result)
{
    return run_task(entry, NULL, NULL, argc, argv, out_app_result);
}

static minishell_platform_result_t run_external(const char *root,
                                                const char *name,
                                                int argc,
                                                char **argv,
                                                int *out_app_result)
{
    return run_task(NULL, root, name, argc, argv, out_app_result);
}

#else

static minishell_platform_result_t run_entry(adv_app_entry_fn entry,
                                             int argc,
                                             char **argv,
                                             int *out_app_result)
{
    *out_app_result = entry(argc, argv);
    return MINISHELL_PLATFORM_OK;
}

static minishell_platform_result_t run_external(const char *root,
                                                const char *name,
                                                int argc,
                                                char **argv,
                                                int *out_app_result)
{
    return adv_elf_loader_app_run(root, name, argc, argv, out_app_result);
}

#endif

typedef struct {
    const char *root;
    minishell_app_emit_fn emit;
    void *ctx;
} external_emit_context_t;

static void emit_external_app(const char *name, void *ctx)
{
    external_emit_context_t *context = (external_emit_context_t *)ctx;

    if (context == NULL || name == NULL || find_static_app(name) != NULL) return;

    /* Flash shadows SD for an external app with the same name. */
    if (strcmp(context->root, "/sd") == 0 &&
        adv_elf_loader_app_exists("/flash", name)) {
        return;
    }

    context->emit(name, context->ctx);
}

minishell_platform_result_t minishell_platform_apps_list(minishell_app_emit_fn emit,
                                                         void *ctx)
{
    minishell_platform_result_t result;
    external_emit_context_t flash_context = {
        .root = "/flash",
        .emit = emit,
        .ctx = ctx,
    };
    external_emit_context_t sd_context = {
        .root = "/sd",
        .emit = emit,
        .ctx = ctx,
    };

    if (emit == NULL) return MINISHELL_PLATFORM_ERR_INVALID;

    for (size_t i = 0u; i < sizeof(s_apps) / sizeof(s_apps[0]); ++i) {
        emit(s_apps[i].name, ctx);
    }

    result = adv_elf_loader_apps_list("/flash", emit_external_app, &flash_context);
    if (result != MINISHELL_PLATFORM_OK) return result;

    result = adv_elf_loader_apps_list("/sd", emit_external_app, &sd_context);
    if (result != MINISHELL_PLATFORM_OK) return result;

    return MINISHELL_PLATFORM_OK;
}

minishell_platform_result_t minishell_platform_app_run(const char *name,
                                                       int argc,
                                                       char **argv,
                                                       int *out_app_result)
{
    const adv_app_entry_t *static_app;

    if (out_app_result == NULL || !valid_app_name(name)) {
        return MINISHELL_PLATFORM_ERR_INVALID;
    }
    *out_app_result = 0;

    static_app = find_static_app(name);
    if (static_app != NULL) {
        return run_entry(static_app->entry, argc, argv, out_app_result);
    }

    if (adv_elf_loader_app_exists("/flash", name)) {
        return run_external("/flash", name, argc, argv, out_app_result);
    }

    if (adv_elf_loader_app_exists("/sd", name)) {
        return run_external("/sd", name, argc, argv, out_app_result);
    }

    return MINISHELL_PLATFORM_ERR_NOT_FOUND;
}

#include <stddef.h>
#include <string.h>

#include "platform_backend.h"

typedef int (*adv_app_entry_fn)(int argc, char **argv);

typedef struct {
    const char *name;
    adv_app_entry_fn entry;
} adv_app_entry_t;

extern int minishell_app_hello_main(int argc, char **argv);
extern int minishell_app_a2_probe_main(int argc, char **argv);
extern int minishell_app_a3_probe_main(int argc, char **argv);
extern int minishell_app_date_main(int argc, char **argv);

static const adv_app_entry_t s_apps[] = {
    {"hello", minishell_app_hello_main},
    {"probe", minishell_app_a2_probe_main},
    {"a3probe", minishell_app_a3_probe_main},
    {"date", minishell_app_date_main},
};

static int valid_app_name(const char *name)
{
    return name != NULL && name[0] != '\0' && strchr(name, '/') == NULL;
}

minishell_platform_result_t minishell_platform_apps_list(minishell_app_emit_fn emit,
                                                         void *ctx)
{
    if (emit == NULL) return MINISHELL_PLATFORM_ERR_INVALID;

    for (size_t i = 0u; i < sizeof(s_apps) / sizeof(s_apps[0]); ++i) {
        emit(s_apps[i].name, ctx);
    }
    return MINISHELL_PLATFORM_OK;
}

minishell_platform_result_t minishell_platform_app_run(const char *name,
                                                       int argc,
                                                       char **argv,
                                                       int *out_app_result)
{
    if (out_app_result == NULL || !valid_app_name(name)) {
        return MINISHELL_PLATFORM_ERR_INVALID;
    }
    *out_app_result = 0;

    for (size_t i = 0u; i < sizeof(s_apps) / sizeof(s_apps[0]); ++i) {
        if (strcmp(name, s_apps[i].name) == 0) {
            *out_app_result = s_apps[i].entry(argc, argv);
            return MINISHELL_PLATFORM_OK;
        }
    }

    return MINISHELL_PLATFORM_ERR_NOT_FOUND;
}

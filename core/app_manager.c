#include "app_manager.h"
#include "minishell_services.h"

minishell_platform_result_t minishell_app_list(minishell_app_emit_fn emit, void *ctx)
{
    return minishell_platform_apps_list(emit, ctx);
}

minishell_platform_result_t minishell_app_run(const char *name,
                                              int argc,
                                              char **argv,
                                              int *out_app_result)
{
    if (out_app_result != NULL) *out_app_result = 0;
    minishell_services_app_begin();
    minishell_platform_result_t result =
        minishell_platform_app_run(name, argc, argv, out_app_result);
    minishell_services_app_end();
    return result;
}

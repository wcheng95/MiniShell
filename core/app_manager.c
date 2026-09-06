#include "app_manager.h"
#include "minishell_services.h"

int minishell_app_list(minishell_app_emit_fn emit, void *ctx)
{
    return minishell_platform_apps_list(emit, ctx);
}

int minishell_app_run(const char *name, int argc, char **argv)
{
    minishell_services_app_begin();
    int result = minishell_platform_app_run(name, argc, argv);
    minishell_services_app_end();
    return result;
}

#include "minishell_runtime.h"

#include "minishell_services.h"
#include "platform_backend.h"
#include "shell.h"

int minishell_run(void)
{
    if (minishell_platform_init() != 0) {
        minishell_platform_console_write("minishell: platform init failed\n");
        return 1;
    }

    minishell_resource_limits_t limits = {0};
    if (minishell_platform_resource_limits(&limits) != 0) {
        minishell_platform_console_write("minishell: invalid resource configuration\n");
        minishell_platform_shutdown();
        return 1;
    }
    minishell_services_set_resource_limits(&limits);

    minishell_services_port_t services_port;
    minishell_platform_services_prepare(&services_port);
    minishell_services_configure(&services_port);
    minishell_platform_services_started();

    minishell_platform_console_write("minishell\n");
    int result = minishell_shell_run();

    minishell_platform_services_stopping();
    minishell_services_configure(NULL);
    minishell_platform_shutdown();
    return result;
}

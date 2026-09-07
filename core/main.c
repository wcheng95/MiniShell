#include <stdio.h>

#include "minishell_services.h"
#include "platform_backend.h"
#include "shell.h"

int main(void)
{
    if (minishell_platform_init() != 0) {
        fprintf(stderr, "MiniShell: platform init failed\n");
        return 1;
    }

    /* The shell and foreground apps share stdin.  Keep stdio from reading
     * ahead across the foreground handoff to the Input ABI. */
    (void)setvbuf(stdin, NULL, _IONBF, 0);

    minishell_resource_limits_t limits = {0};
    if (minishell_platform_resource_limits(&limits) != 0) {
        fprintf(stderr, "MiniShell: invalid resource configuration\n");
        minishell_platform_shutdown();
        return 1;
    }
    minishell_services_set_resource_limits(&limits);
    minishell_services_configure(minishell_platform_services_port());

    puts("MiniShell");
    int result = minishell_shell_run();

    minishell_services_configure(NULL);
    minishell_platform_shutdown();
    return result;
}

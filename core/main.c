#include <stdio.h>

#include "platform_backend.h"
#include "shell.h"

int main(void)
{
    if (minishell_platform_init() != 0) {
        fprintf(stderr, "MiniShell: platform init failed\n");
        return 1;
    }

    puts("MiniShell");
    int result = minishell_shell_run();

    minishell_platform_shutdown();
    return result;
}

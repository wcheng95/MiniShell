#include <stdio.h>

#include "minishell_app.h"
#include "minishell_platform.h"
#include "minishell_shell.h"

void app_main(void)
{
    printf("\nMiniShell\n");

    /* Platform failures are intentionally non-fatal. The shell is the
     * diagnostic environment, so it must remain available when SD or an
     * optional platform facility fails. */
    (void)minishell_platform_init();

    if (minishell_app_init() != 0) {
        printf("warning: ELF app manager initialization failed\n");
    }

    minishell_shell_run();
}

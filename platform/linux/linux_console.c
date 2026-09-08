#include <limits.h>
#include <stdio.h>

#include "linux_internal.h"
#include "platform_backend.h"

void linux_console_prepare(void)
{
    /* The resident shell and foreground applications share stdin.  Prevent
     * stdio from reading ahead across the handoff to the Input API. */
    (void)setvbuf(stdin, NULL, _IONBF, 0);
}

void minishell_platform_console_write(const char *text)
{
    if (text == NULL) return;
    fputs(text, stdout);
    fflush(stdout);
}

int minishell_platform_console_read_line(char *buffer, size_t capacity)
{
    if (buffer == NULL || capacity == 0u || capacity > (size_t)INT_MAX) return -1;

    if (fgets(buffer, (int)capacity, stdin) != NULL) return 1;
    return ferror(stdin) ? -1 : 0;
}

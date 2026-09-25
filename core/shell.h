#pragma once

int minishell_shell_run(void);

/* Consumes a mutable semicolon-delimited boot setting; ignores exit requests. */
void minishell_shell_startup(char *commands);

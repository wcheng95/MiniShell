#pragma once

#ifdef __cplusplus
extern "C" {
#endif

int minishell_app_init(void);
int minishell_app_run(const char *command, int argc, char **argv);

#ifdef __cplusplus
}
#endif

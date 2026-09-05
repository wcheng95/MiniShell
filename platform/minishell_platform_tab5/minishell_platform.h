#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

int minishell_platform_init(void);
bool minishell_platform_sd_ready(void);
const char *minishell_platform_sd_status(void);
const char *minishell_platform_console_status(void);
const char *minishell_platform_name(void);

#ifdef __cplusplus
}
#endif

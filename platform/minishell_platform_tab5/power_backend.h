#pragma once

#include "esp_err.h"
#include "minishell_power.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t minishell_tab5_power_init(void);
int minishell_tab5_power_get_status(void *ctx, minishell_power_status_t *out_status);
int minishell_tab5_power_suspend(void *ctx);
int minishell_tab5_power_poweroff(void *ctx);

#ifdef __cplusplus
}
#endif

#pragma once

#include "minishell/api.h"

#ifdef __cplusplus
extern "C" {
#endif

mini_result_t app_controller_init(const mini_api_t *api);
int app_controller_run(void);
void app_controller_shutdown(void);

#ifdef __cplusplus
}
#endif

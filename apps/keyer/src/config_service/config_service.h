#pragma once

#include <stdbool.h>

#include "keyer_types.h"
#include "minishell/api.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KEYER_SETTING_PATH "/flash/keyer/setting.txt"

void config_service_defaults(keyer_config_t *config);
mini_result_t config_service_load(const mini_api_t *api,
                                  keyer_config_t *config,
                                  bool *out_loaded_from_file);

#ifdef __cplusplus
}
#endif

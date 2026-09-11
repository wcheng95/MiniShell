#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "minishell/api.h"

#ifdef __cplusplus
extern "C" {
#endif

int adv_rtc_prepare(void);
bool adv_rtc_ready(void);
mini_result_t adv_rtc_load_utc(int64_t *out_seconds, uint32_t *out_nanoseconds);
mini_result_t adv_rtc_store_utc(int64_t seconds, uint32_t nanoseconds);

#ifdef __cplusplus
}
#endif

#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

int adv_gps_prepare(void);
void adv_gps_shutdown(void);
bool adv_gps_ready(void);
int adv_gps_active_baud(void);
bool adv_gps_baud_locked(void);

#ifdef __cplusplus
}
#endif

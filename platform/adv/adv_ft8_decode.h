#ifndef ADV_FT8_DECODE_H
#define ADV_FT8_DECODE_H

#include <stdbool.h>

#include "app_controller.h"

#ifdef __cplusplus
extern "C" {
#endif

bool adv_ft8_decode_worker_start(AppController *app);
void adv_ft8_decode_worker_stop(AppController *app);

#ifdef __cplusplus
}
#endif

#endif

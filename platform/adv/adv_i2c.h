#pragma once

#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

int adv_i2c_prepare(void);
i2c_master_bus_handle_t adv_i2c_bus(void);

#ifdef __cplusplus
}
#endif

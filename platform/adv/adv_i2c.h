#pragma once

#include "driver/i2c_master.h"

#define ADV_I2C_DEFAULT_SDA_GPIO 8
#define ADV_I2C_DEFAULT_SCL_GPIO 9

#ifdef __cplusplus
extern "C" {
#endif

/* Configure the shared ADV I2C bus before the first adv_i2c_prepare().
 * The current platform default remains SDA=G8, SCL=G9. */
int adv_i2c_configure_pins(int sda_gpio, int scl_gpio);
void adv_i2c_get_pins(int *out_sda_gpio, int *out_scl_gpio);
int adv_i2c_prepare(void);
i2c_master_bus_handle_t adv_i2c_bus(void);

#ifdef __cplusplus
}
#endif

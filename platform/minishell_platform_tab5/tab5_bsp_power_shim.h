#pragma once

/*
 * Narrow, platform-private view of the Tab5 BSP used by MiniShell power code.
 *
 * Do not include the umbrella <bsp/m5stack_tab5.h> here. The no-graphics BSP
 * currently pulls display.h from that header, which in turn exposes an esp_lcd
 * header that is a private BSP dependency. MiniShell power management needs only
 * the shared I2C bus and the second IO expander, so keep that dependency surface
 * explicit and small.
 */

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_io_expander.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t bsp_i2c_init(void);
i2c_master_bus_handle_t bsp_i2c_get_handle(void);
esp_io_expander_handle_t bsp_io_expander1_init(void);

#ifdef __cplusplus
}
#endif

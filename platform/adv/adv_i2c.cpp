#include "adv_i2c.h"

#include "driver/i2c_master.h"
#include "esp_err.h"

namespace {
constexpr gpio_num_t kSda = GPIO_NUM_8;
constexpr gpio_num_t kScl = GPIO_NUM_9;

i2c_master_bus_handle_t s_bus = nullptr;
}

extern "C" int adv_i2c_prepare(void)
{
    if (s_bus != nullptr) return 0;

    i2c_master_bus_config_t config = {};
    config.i2c_port = I2C_NUM_0;
    config.sda_io_num = kSda;
    config.scl_io_num = kScl;
    config.clk_source = I2C_CLK_SRC_DEFAULT;
    config.glitch_ignore_cnt = 7;
    config.flags.enable_internal_pullup = true;

    esp_err_t err = i2c_new_master_bus(&config, &s_bus);
    if (err == ESP_ERR_INVALID_STATE) {
        err = i2c_master_get_bus_handle(I2C_NUM_0, &s_bus);
    }
    if (err != ESP_OK || s_bus == nullptr) {
        s_bus = nullptr;
        return -1;
    }
    return 0;
}

extern "C" i2c_master_bus_handle_t adv_i2c_bus(void)
{
    return s_bus;
}

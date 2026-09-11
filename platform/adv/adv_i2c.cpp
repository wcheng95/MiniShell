#include "adv_i2c.h"

#include "driver/i2c_master.h"
#include "esp_err.h"

namespace {
int s_sda_gpio = ADV_I2C_DEFAULT_SDA_GPIO;
int s_scl_gpio = ADV_I2C_DEFAULT_SCL_GPIO;
i2c_master_bus_handle_t s_bus = nullptr;
}

extern "C" int adv_i2c_configure_pins(int sda_gpio, int scl_gpio)
{
    if (s_bus != nullptr || sda_gpio < 0 || scl_gpio < 0 || sda_gpio == scl_gpio) return -1;
    s_sda_gpio = sda_gpio;
    s_scl_gpio = scl_gpio;
    return 0;
}

extern "C" void adv_i2c_get_pins(int *out_sda_gpio, int *out_scl_gpio)
{
    if (out_sda_gpio != nullptr) *out_sda_gpio = s_sda_gpio;
    if (out_scl_gpio != nullptr) *out_scl_gpio = s_scl_gpio;
}

extern "C" int adv_i2c_prepare(void)
{
    if (s_bus != nullptr) return 0;

    i2c_master_bus_config_t config = {};
    config.i2c_port = I2C_NUM_0;
    config.sda_io_num = static_cast<gpio_num_t>(s_sda_gpio);
    config.scl_io_num = static_cast<gpio_num_t>(s_scl_gpio);
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

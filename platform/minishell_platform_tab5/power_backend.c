#include "power_backend.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "tab5_bsp_power_shim.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define INA226_ADDRESS              0x41u
#define INA226_REG_BUS_VOLTAGE      0x02u
#define INA226_I2C_HZ               100000u
#define INA226_TIMEOUT_MS           100

#define TAB5_CHARGE_ENABLE          IO_EXPANDER_PIN_NUM_7
#define TAB5_CHARGE_STATUS          IO_EXPANDER_PIN_NUM_6
#define TAB5_QUICK_CHARGE_ENABLE_N  IO_EXPANDER_PIN_NUM_5
#define TAB5_POWEROFF_PULSE         IO_EXPANDER_PIN_NUM_4

static esp_io_expander_handle_t s_power_expander;
static i2c_master_dev_handle_t s_ina226;
static bool s_ready;

static esp_err_t expander_output(uint32_t pin, uint8_t level)
{
    esp_err_t err = esp_io_expander_set_dir(s_power_expander, pin, IO_EXPANDER_OUTPUT);
    if (err != ESP_OK) return err;
    err = esp_io_expander_set_output_mode(s_power_expander, pin,
                                          IO_EXPANDER_OUTPUT_MODE_PUSH_PULL);
    if (err != ESP_OK) return err;
    return esp_io_expander_set_level(s_power_expander, pin, level);
}

static esp_err_t init_power_expander(void)
{
    s_power_expander = bsp_io_expander1_init();
    if (s_power_expander == NULL) return ESP_FAIL;

    /* Match the Tab5's normal powered behavior: charging enabled, the charger's
     * quick-charge control enabled (active-low), and the poweroff pulse idle low. */
    esp_err_t err = expander_output(TAB5_CHARGE_ENABLE, 1u);
    if (err != ESP_OK) return err;
    err = expander_output(TAB5_QUICK_CHARGE_ENABLE_N, 0u);
    if (err != ESP_OK) return err;
    err = expander_output(TAB5_POWEROFF_PULSE, 0u);
    if (err != ESP_OK) return err;

    err = esp_io_expander_set_dir(s_power_expander, TAB5_CHARGE_STATUS,
                                  IO_EXPANDER_INPUT);
    if (err != ESP_OK) return err;
    (void)esp_io_expander_set_pullupdown(s_power_expander, TAB5_CHARGE_STATUS,
                                         IO_EXPANDER_PULL_DOWN);
    return ESP_OK;
}

static esp_err_t init_ina226(void)
{
    i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = INA226_ADDRESS,
        .scl_speed_hz = INA226_I2C_HZ,
    };
    return i2c_master_bus_add_device(bsp_i2c_get_handle(), &config, &s_ina226);
}

static esp_err_t read_ina226_u16(uint8_t reg, uint16_t *out_value)
{
    if (s_ina226 == NULL || out_value == NULL) return ESP_ERR_INVALID_STATE;

    uint8_t bytes[2] = {0u, 0u};
    esp_err_t err = i2c_master_transmit_receive(s_ina226, &reg, 1u,
                                                bytes, sizeof(bytes),
                                                INA226_TIMEOUT_MS);
    if (err != ESP_OK) return err;
    *out_value = (uint16_t)(((uint16_t)bytes[0] << 8u) | bytes[1]);
    return ESP_OK;
}

static uint8_t battery_percent_from_pack_mv(uint32_t pack_mv)
{
    /* Follow M5Unified's current Tab5 2S estimate: normalize each cell over
     * roughly 3.30 V .. 4.10 V and clamp to 0..100%. */
    int32_t cell_mv = (int32_t)(pack_mv / 2u);
    int32_t percent = (cell_mv - 3300) * 100 / 800;
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    return (uint8_t)percent;
}

esp_err_t minishell_tab5_power_init(void)
{
    s_ready = false;
    s_power_expander = NULL;
    s_ina226 = NULL;

    esp_err_t err = bsp_i2c_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;

    err = init_power_expander();
    if (err != ESP_OK) return err;

    err = init_ina226();
    if (err != ESP_OK) return err;

    s_ready = true;
    return ESP_OK;
}

int minishell_tab5_power_get_status(void *ctx, minishell_power_status_t *out_status)
{
    (void)ctx;
    if (!s_ready || out_status == NULL) return -1;

    uint16_t raw_bus = 0u;
    if (read_ina226_u16(INA226_REG_BUS_VOLTAGE, &raw_bus) == ESP_OK) {
        /* INA226 bus-voltage register: 1.25 mV/LSB. */
        uint32_t pack_mv = ((uint32_t)raw_bus * 5u + 2u) / 4u;
        out_status->battery_percent_valid = true;
        out_status->battery_percent = battery_percent_from_pack_mv(pack_mv);
    }

    uint32_t charge_level = 0u;
    if (esp_io_expander_get_level(s_power_expander, TAB5_CHARGE_STATUS,
                                  &charge_level) == ESP_OK) {
        out_status->charging_valid = true;
        out_status->charging = (charge_level & TAB5_CHARGE_STATUS) != 0u;
    }

    return 0;
}

int minishell_tab5_power_suspend(void *ctx)
{
    (void)ctx;
    if (!s_ready) return -1;

    fflush(NULL);
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    esp_deep_sleep_start();
    return -1;
}

int minishell_tab5_power_poweroff(void *ctx)
{
    (void)ctx;
    if (!s_ready) return -1;

    fflush(NULL);

    /* Current M5Unified Tab5 poweroff sequence pulses E2.P4. If external power
     * keeps the board alive, fall back to deep sleep rather than returning to a
     * half-shutdown shell. */
    for (unsigned i = 0u; i < 10u; ++i) {
        if (esp_io_expander_set_level(s_power_expander, TAB5_POWEROFF_PULSE,
                                      (uint8_t)(i & 1u)) != ESP_OK) {
            return -1;
        }
        vTaskDelay(pdMS_TO_TICKS(50u));
    }

    esp_deep_sleep_start();
    return -1;
}

#include "adv_internal.h"

#include <string.h>

#include <M5Unified.h>

#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_check.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

namespace {
constexpr adc_unit_t kAdcUnit = ADC_UNIT_1;
constexpr adc_channel_t kBatteryChannel = ADC_CHANNEL_9;  // GPIO10
constexpr float kBatteryDivider = 2.0f;

adc_oneshot_unit_handle_t s_adc = nullptr;
adc_cali_handle_t s_cali = nullptr;
bool s_initialized = false;
bool s_cali_ok = false;

int voltage_to_percent(int mv)
{
    if (mv >= 4200) return 100;
    if (mv >= 4100) return 90;
    if (mv >= 4000) return 80;
    if (mv >= 3900) return 65;
    if (mv >= 3800) return 50;
    if (mv >= 3700) return 35;
    if (mv >= 3600) return 20;
    if (mv >= 3500) return 10;
    if (mv >= 3400) return 5;
    return 0;
}

esp_err_t prepare_battery_adc()
{
    if (s_initialized) return ESP_OK;

    adc_oneshot_unit_init_cfg_t unit_cfg = {};
    unit_cfg.unit_id = kAdcUnit;
    ESP_RETURN_ON_ERROR(adc_oneshot_new_unit(&unit_cfg, &s_adc),
                        "adv_power", "adc unit");

    adc_oneshot_chan_cfg_t chan_cfg = {};
    chan_cfg.atten = ADC_ATTEN_DB_12;
    chan_cfg.bitwidth = ADC_BITWIDTH_12;
    ESP_RETURN_ON_ERROR(adc_oneshot_config_channel(s_adc, kBatteryChannel, &chan_cfg),
                        "adv_power", "adc channel");

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cali_cfg = {};
    cali_cfg.unit_id = kAdcUnit;
    cali_cfg.chan = kBatteryChannel;
    cali_cfg.atten = ADC_ATTEN_DB_12;
    cali_cfg.bitwidth = ADC_BITWIDTH_12;
    if (adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_cali) == ESP_OK)
        s_cali_ok = true;
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_line_fitting_config_t cali_cfg = {};
    cali_cfg.unit_id = kAdcUnit;
    cali_cfg.atten = ADC_ATTEN_DB_12;
    cali_cfg.bitwidth = ADC_BITWIDTH_12;
    if (adc_cali_create_scheme_line_fitting(&cali_cfg, &s_cali) == ESP_OK)
        s_cali_ok = true;
#endif

    s_initialized = true;
    return ESP_OK;
}
}

extern "C" mini_result_t adv_battery_read(int *out_voltage_mv, int *out_percent)
{
    if (out_voltage_mv == nullptr || out_percent == nullptr)
        return MINI_ERR_INVALID;
    *out_voltage_mv = -1;
    *out_percent = -1;

    if (prepare_battery_adc() != ESP_OK)
        return MINI_ERR_IO;

    int raw = 0;
    if (adc_oneshot_read(s_adc, kBatteryChannel, &raw) != ESP_OK)
        return MINI_ERR_IO;

    int adc_mv = raw;
    if (s_cali_ok && s_cali != nullptr) {
        if (adc_cali_raw_to_voltage(s_cali, raw, &adc_mv) != ESP_OK)
            return MINI_ERR_IO;
    }

    const int battery_mv = static_cast<int>(adc_mv * kBatteryDivider + 0.5f);
    *out_voltage_mv = battery_mv;
    *out_percent = voltage_to_percent(battery_mv);
    return MINI_OK;
}

extern "C" mini_result_t adv_enter_deep_sleep(void)
{
    if (adv_display_ready())
        M5.Display.sleep();

    vTaskDelay(pdMS_TO_TICKS(100));

    if (esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, 0) != ESP_OK)
        return MINI_ERR_IO;

    esp_deep_sleep_start();
    return MINI_ERR_IO;
}

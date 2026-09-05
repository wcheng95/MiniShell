#include <stdio.h>

#include "bsp/esp-bsp.h"
#include "esp_err.h"

#include "minishell_platform.h"

static esp_err_t s_sd_status = ESP_FAIL;

int minishell_platform_init(void)
{
    s_sd_status = bsp_sdcard_mount();

    if (s_sd_status == ESP_OK) {
        printf("sd: mounted at /sd\n");
        return 0;
    }

    printf("sd: mount failed: %s (0x%x)\n",
           esp_err_to_name(s_sd_status),
           (unsigned int)s_sd_status);
    return -1;
}

bool minishell_platform_sd_ready(void)
{
    return s_sd_status == ESP_OK;
}

const char *minishell_platform_sd_status(void)
{
    if (s_sd_status == ESP_OK) {
        return "OK";
    }

    return esp_err_to_name(s_sd_status);
}

const char *minishell_platform_name(void)
{
    return "M5Stack Tab5 / ESP32-P4";
}

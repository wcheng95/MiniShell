#include <stdio.h>

#include "esp_err.h"

#include "minishell_platform.h"

/*
 * The current m5stack_tab5_noglib public umbrella header pulls in display.h,
 * which in turn requires esp_lcd headers that the BSP declares privately.
 * Task 0 needs only the SD mount entry point, so keep that BSP detail isolated
 * here instead of leaking display dependencies into MiniShell's platform code.
 */
esp_err_t bsp_sdcard_mount(void);

static esp_err_t s_console_status = ESP_FAIL;
static esp_err_t s_sd_status = ESP_FAIL;

static esp_err_t init_console(void)
{
    /*
     * ESP-IDF owns initialization of the primary USB Serial/JTAG console when
     * CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG is selected. Its VFS setup installs
     * the interrupt-driven USB Serial/JTAG driver, so normal getchar()/stdio
     * input can block while waiting for shell input.
     *
     * MiniShell only chooses stdio buffering policy here; it does not own or
     * reconfigure the USB Serial/JTAG hardware.
     */
    setvbuf(stdin, NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IONBF, 0);
    return ESP_OK;
}

int minishell_platform_init(void)
{
    int result = 0;

    s_console_status = init_console();
    if (s_console_status != ESP_OK) {
        printf("console: setup failed: %s (0x%x)\n",
               esp_err_to_name(s_console_status),
               (unsigned int)s_console_status);
        result = -1;
    }

    s_sd_status = bsp_sdcard_mount();

    if (s_sd_status == ESP_OK) {
        printf("sd: mounted at /sd\n");
    } else {
        printf("sd: mount failed: %s (0x%x)\n",
               esp_err_to_name(s_sd_status),
               (unsigned int)s_sd_status);
        result = -1;
    }

    return result;
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

const char *minishell_platform_console_status(void)
{
    if (s_console_status == ESP_OK) {
        return "OK - USB Serial/JTAG";
    }

    return esp_err_to_name(s_console_status);
}

const char *minishell_platform_name(void)
{
    return "M5Stack Tab5 / ESP32-P4";
}

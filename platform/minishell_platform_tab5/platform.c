#include <stdio.h>

#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
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
     * Selecting CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG registers the USB
     * Serial/JTAG VFS for stdin/stdout, but its default implementation polls
     * the peripheral directly. A shell waiting in getchar() would therefore
     * keep CPU0 busy and starve IDLE0, eventually tripping the task watchdog.
     *
     * Install the interrupt-driven driver and tell the VFS to use it. Blocking
     * stdin then sleeps on the driver's FreeRTOS objects and yields the CPU.
     */
    if (!usb_serial_jtag_is_driver_installed()) {
        usb_serial_jtag_driver_config_t config = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
        esp_err_t err = usb_serial_jtag_driver_install(&config);
        if (err != ESP_OK) {
            return err;
        }
    }

    usb_serial_jtag_vfs_use_driver();

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
        return "OK - USB Serial/JTAG (interrupt-driven)";
    }

    return esp_err_to_name(s_console_status);
}

const char *minishell_platform_name(void)
{
    return "M5Stack Tab5 / ESP32-P4";
}

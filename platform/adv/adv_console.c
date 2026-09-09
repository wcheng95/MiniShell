#include <stddef.h>
#include <stdio.h>

#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "adv_internal.h"
#include "platform_backend.h"

int adv_console_prepare(void)
{
    (void)setvbuf(stdin, NULL, _IONBF, 0);
    (void)setvbuf(stdout, NULL, _IONBF, 0);

    usb_serial_jtag_driver_config_t config = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    esp_err_t err = usb_serial_jtag_driver_install(&config);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return -1;
    }

    usb_serial_jtag_vfs_use_driver();
    return 0;
}

void minishell_platform_console_write(const char *text)
{
    if (text == NULL) return;
    (void)fputs(text, stdout);
    (void)fflush(stdout);
}

int minishell_platform_console_read_line(char *buffer, size_t capacity)
{
    if (buffer == NULL || capacity == 0u) return -1;

    size_t length = 0u;
    buffer[0] = '\0';

    for (;;) {
        int ch = fgetc(stdin);
        if (ch == EOF) {
            clearerr(stdin);
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        if (ch == 0x04 && length == 0u) {
            return 0;
        }

        if (ch == '\r' || ch == '\n') {
            buffer[length] = '\0';
            minishell_platform_console_write("\n");
            return 1;
        }

        if (ch == '\b' || ch == 0x7f) {
            if (length != 0u) {
                --length;
                minishell_platform_console_write("\b \b");
            }
            continue;
        }

        if (ch >= 0x20 && ch <= 0x7e && length + 1u < capacity) {
            char echo[2] = {(char)ch, '\0'};
            buffer[length++] = (char)ch;
            buffer[length] = '\0';
            minishell_platform_console_write(echo);
        }
    }
}

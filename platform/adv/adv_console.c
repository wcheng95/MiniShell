#include <fcntl.h>
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
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return -1;

    usb_serial_jtag_vfs_use_driver();

    int flags = fcntl(fileno(stdin), F_GETFL, 0);
    if (flags >= 0) (void)fcntl(fileno(stdin), F_SETFL, flags | O_NONBLOCK);
    return 0;
}

int adv_console_suspend_for_usb(void)
{
    if (!usb_serial_jtag_is_driver_installed()) return 0;

    (void)fflush(stdout);
    (void)usb_serial_jtag_wait_tx_done(pdMS_TO_TICKS(100));
    usb_serial_jtag_vfs_use_nonblocking();
    return usb_serial_jtag_driver_uninstall() == ESP_OK ? 0 : -1;
}

int adv_console_resume_after_usb(void)
{
    return adv_console_prepare();
}

void adv_console_debug_write(const char *text)
{
    if (text == NULL) return;
    (void)fputs(text, stdout);
    (void)fflush(stdout);
}

void minishell_platform_console_write(const char *text)
{
    if (text == NULL) return;
    adv_console_debug_write(text);
    adv_display_console_write(text);
}

static int accept_character(int ch, char *buffer, size_t capacity, size_t *length)
{
    if (ch == 0x04 && *length == 0u) return -1;

    if (ch == '\r' || ch == '\n') {
        buffer[*length] = '\0';
        minishell_platform_console_write("\n");
        return 1;
    }

    if (ch == '\b' || ch == 0x7f) {
        if (*length != 0u) {
            --(*length);
            buffer[*length] = '\0';
            minishell_platform_console_write("\b \b");
        }
        return 0;
    }

    if (ch >= 0x20 && ch <= 0x7e && *length + 1u < capacity) {
        char echo[2] = {(char)ch, '\0'};
        buffer[(*length)++] = (char)ch;
        buffer[*length] = '\0';
        minishell_platform_console_write(echo);
    }
    return 0;
}

static int accept_key_event(const mini_key_event_t *event, char *buffer, size_t capacity,
                            size_t *length)
{
    if (event->type == MINI_KEY_EVENT_CHAR && event->codepoint <= 0x7fu) {
        return accept_character((int)event->codepoint, buffer, capacity, length);
    }
    if (event->type != MINI_KEY_EVENT_SPECIAL) return 0;

    if (event->key == MINI_KEY_ENTER) return accept_character('\n', buffer, capacity, length);
    if (event->key == MINI_KEY_BACKSPACE || event->key == MINI_KEY_DELETE) {
        return accept_character('\b', buffer, capacity, length);
    }
    return 0;
}

int minishell_platform_console_read_line(char *buffer, size_t capacity)
{
    if (buffer == NULL || capacity == 0u) return -1;

    size_t length = 0u;
    buffer[0] = '\0';

    for (;;) {
        mini_key_event_t event = {.struct_size = sizeof(event)};
        if (adv_keyboard_read_event(&event) == MINI_OK) {
            int accepted = accept_key_event(&event, buffer, capacity, &length);
            if (accepted > 0) return 1;
            if (accepted < 0) return 0;
        }

        int ch = fgetc(stdin);
        if (ch != EOF) {
            int accepted = accept_character(ch, buffer, capacity, &length);
            if (accepted > 0) return 1;
            if (accepted < 0) return 0;
        } else {
            clearerr(stdin);
        }

        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

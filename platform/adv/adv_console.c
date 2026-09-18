#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/lock.h>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "adv_internal.h"
#include "adv_usb_console_handoff.h"
#include "platform_backend.h"

static adv_usb_console_handoff_t s_host_console;
static _lock_t s_output_lock;
static bool s_uart_installed, s_uart_active, s_uart_pins;
static vprintf_like_t s_previous_log;

static int uart_log(const char *format, va_list args)
{
    _lock_acquire_recursive(&s_output_lock);
    if (!s_uart_active) {
        vprintf_like_t previous = s_previous_log;
        _lock_release_recursive(&s_output_lock);
        return previous(format, args);
    }
    /* Fixed diagnostic buffer: never allocate on the capture/class task stack
     * beyond this bound. No flow control or UART input is required for FT8. */
    char line[512];
    int length = vsnprintf(line, sizeof(line), format, args);
    if (length > 0) {
        size_t count = (size_t)length < sizeof(line) ? (size_t)length : sizeof(line) - 1u;
        (void)uart_write_bytes(UART_NUM_0, line, count);
    }
    _lock_release_recursive(&s_output_lock);
    return length;
}

static int debug_uart_begin(void)
{
    if (uart_is_driver_installed(UART_NUM_0)) return -1;
    uart_config_t config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    if (uart_driver_install(UART_NUM_0, 256, 2048, 0, NULL, 0) != ESP_OK) return -1;
    s_uart_installed = true;
    if (uart_param_config(UART_NUM_0, &config) != ESP_OK) return -1;
    s_uart_pins = true;
    if (uart_set_pin(UART_NUM_0, GPIO_NUM_4, GPIO_NUM_5,
                     UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK) return -1;
    _lock_acquire_recursive(&s_output_lock);
    s_uart_active = true;
    s_previous_log = esp_log_set_vprintf(uart_log);
    _lock_release_recursive(&s_output_lock);
    adv_console_debug_write("ADV: USB Host diagnostics on UART0 TX=GPIO4 RX=GPIO5 115200\n");
    return 0;
}

static int debug_uart_end(void)
{
    _lock_acquire_recursive(&s_output_lock);
    if (s_uart_installed) {
        (void)uart_wait_tx_done(UART_NUM_0, pdMS_TO_TICKS(100));
        bool was_active = s_uart_active;
        s_uart_active = false;
        if (uart_driver_delete(UART_NUM_0) != ESP_OK) {
            s_uart_active = was_active;
            _lock_release_recursive(&s_output_lock);
            return -1;
        }
        s_uart_installed = false;
        if (was_active) (void)esp_log_set_vprintf(s_previous_log);
    }
    if (s_uart_pins) {
        esp_err_t tx = gpio_reset_pin(GPIO_NUM_4);
        esp_err_t rx = gpio_reset_pin(GPIO_NUM_5);
        if (tx != ESP_OK || rx != ESP_OK) {
            _lock_release_recursive(&s_output_lock);
            return -1;
        }
        s_uart_pins = false;
    }
    _lock_release_recursive(&s_output_lock);
    return 0;
}

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
    /* A failed UAC teardown may leave the foreground shell usable locally.
     * Refuse a competing MSC handoff until that host lease has been released. */
    if (s_host_console.suspended) return -1;
    if (!usb_serial_jtag_is_driver_installed()) return 0;

    (void)fflush(stdout);
    (void)usb_serial_jtag_wait_tx_done(pdMS_TO_TICKS(100));
    usb_serial_jtag_vfs_use_nonblocking();
    if (usb_serial_jtag_driver_uninstall() == ESP_OK) return 0;
    usb_serial_jtag_vfs_use_driver();
    return -1;
}

int adv_console_resume_after_usb(void)
{
    return adv_console_prepare();
}

static const adv_usb_console_ops_t s_host_console_ops = {
    adv_console_suspend_for_usb, debug_uart_begin, debug_uart_end, adv_console_resume_after_usb
};

int adv_console_begin_usb_host(void)
{
    return adv_usb_console_begin(&s_host_console, &s_host_console_ops) ? 0 : -1;
}

int adv_console_end_usb_host(bool usb_busy)
{
    return adv_usb_console_end(&s_host_console, &s_host_console_ops, usb_busy) ? 0 : -1;
}

void adv_console_debug_write(const char *text)
{
    if (text == NULL) return;
    _lock_acquire_recursive(&s_output_lock);
    if (s_uart_active) {
        (void)uart_write_bytes(UART_NUM_0, text, strlen(text));
    } else {
        (void)fputs(text, stdout);
        (void)fflush(stdout);
    }
    _lock_release_recursive(&s_output_lock);
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

        int ch = s_host_console.suspended ? EOF : fgetc(stdin);
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

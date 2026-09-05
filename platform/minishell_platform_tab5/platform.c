#include <stdio.h>

#include "bsp/esp-bsp.h"
#include "driver/uart.h"
#include "driver/uart_vfs.h"
#include "esp_err.h"

#include "minishell_platform.h"

#define MINISHELL_UART UART_NUM_0
#define MINISHELL_UART_TX 37
#define MINISHELL_UART_RX 38
#define MINISHELL_UART_BAUD 115200

static esp_err_t s_console_status = ESP_FAIL;
static esp_err_t s_sd_status = ESP_FAIL;

static esp_err_t init_uart_console(void)
{
    esp_err_t err;

    if (!uart_is_driver_installed(MINISHELL_UART)) {
        err = uart_driver_install(MINISHELL_UART, 2048, 0, 0, NULL, 0);
        if (err != ESP_OK) {
            return err;
        }
    }

    const uart_config_t config = {
        .baud_rate = MINISHELL_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    err = uart_param_config(MINISHELL_UART, &config);
    if (err != ESP_OK) {
        return err;
    }

    err = uart_set_pin(MINISHELL_UART,
                       MINISHELL_UART_TX,
                       MINISHELL_UART_RX,
                       UART_PIN_NO_CHANGE,
                       UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        return err;
    }

    /* ESP-IDF's basic UART VFS reads are non-blocking. Switch stdin/stdout to
     * the installed UART driver's blocking, interrupt-driven implementation. */
    uart_vfs_dev_port_set_rx_line_endings(MINISHELL_UART, ESP_LINE_ENDINGS_LF);
    uart_vfs_dev_port_set_tx_line_endings(MINISHELL_UART, ESP_LINE_ENDINGS_CRLF);
    uart_vfs_dev_use_driver(MINISHELL_UART);

    setvbuf(stdin, NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IONBF, 0);

    return ESP_OK;
}

int minishell_platform_init(void)
{
    int result = 0;

    s_console_status = init_uart_console();
    if (s_console_status != ESP_OK) {
        printf("console: UART driver setup failed: %s (0x%x)\n",
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
        return "OK - UART0 115200 8N1, TX=G37 RX=G38";
    }

    return esp_err_to_name(s_console_status);
}

const char *minishell_platform_name(void)
{
    return "M5Stack Tab5 / ESP32-P4";
}

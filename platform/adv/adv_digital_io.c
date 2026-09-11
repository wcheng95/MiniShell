#include "adv_internal.h"

#include <stdint.h>

#include "driver/gpio.h"

static bool line_available_to_apps(uint32_t line_id)
{
    /* Cardputer ADV Digital I/O is deliberately restricted to GPIOs exposed
     * as application-facing connector signals that are not already owned by
     * resident MiniShell peripherals. Shared I2C/SD pins and all built-in
     * LCD/audio/keyboard/IR/battery pins are excluded by construction.
     *
     * HY2.0-4P: G1, G2
     * EXT 2.54-14P application signals: G3, G4, G5, G6, G13, G15
     */
    switch (line_id) {
    case 1u:
    case 2u:
    case 3u:
    case 4u:
    case 5u:
    case 6u:
    case 13u:
    case 15u:
        return true;
    default:
        return false;
    }
}

static gpio_num_t handle_gpio(minishell_backend_digital_t line)
{
    if (line == MINISHELL_BACKEND_DIGITAL_INVALID) return GPIO_NUM_NC;
    uintptr_t raw = line - 1u;
    if (raw > (uintptr_t)GPIO_NUM_MAX) return GPIO_NUM_NC;
    if (!line_available_to_apps((uint32_t)raw)) return GPIO_NUM_NC;
    return (gpio_num_t)raw;
}

static mini_result_t digital_open(void *ctx, uint32_t line_id, uint32_t mode,
                                  uint32_t initial_level,
                                  minishell_backend_digital_t *out_line)
{
    (void)ctx;
    if (out_line == NULL || initial_level > 1u || line_id > (uint32_t)GPIO_NUM_MAX) {
        return MINI_ERR_INVALID;
    }
    *out_line = MINISHELL_BACKEND_DIGITAL_INVALID;
    if (!line_available_to_apps(line_id)) return MINI_ERR_ACCESS;

    gpio_num_t gpio = (gpio_num_t)line_id;
    if (!GPIO_IS_VALID_GPIO(gpio)) return MINI_ERR_INVALID;

    gpio_config_t config = {
        .pin_bit_mask = 1ULL << line_id,
        .mode = GPIO_MODE_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    switch (mode) {
    case MINI_DIGITAL_IO_MODE_INPUT:
        config.mode = GPIO_MODE_INPUT;
        break;
    case MINI_DIGITAL_IO_MODE_INPUT_PULLUP:
        config.mode = GPIO_MODE_INPUT;
        config.pull_up_en = GPIO_PULLUP_ENABLE;
        break;
    case MINI_DIGITAL_IO_MODE_OUTPUT:
        if (!GPIO_IS_VALID_OUTPUT_GPIO(gpio)) return MINI_ERR_ACCESS;
        config.mode = GPIO_MODE_OUTPUT;
        break;
    case MINI_DIGITAL_IO_MODE_OUTPUT_OPEN_DRAIN:
        if (!GPIO_IS_VALID_OUTPUT_GPIO(gpio)) return MINI_ERR_ACCESS;
        config.mode = GPIO_MODE_OUTPUT_OD;
        break;
    default:
        return MINI_ERR_INVALID;
    }

    if (mode == MINI_DIGITAL_IO_MODE_OUTPUT ||
        mode == MINI_DIGITAL_IO_MODE_OUTPUT_OPEN_DRAIN) {
        /* Set the output latch before enabling drive, then set it again after
         * configuration. This minimizes unintended transitions on open. */
        if (gpio_set_level(gpio, initial_level) != ESP_OK) return MINI_ERR_IO;
    }

    if (gpio_config(&config) != ESP_OK) return MINI_ERR_IO;

    if ((mode == MINI_DIGITAL_IO_MODE_OUTPUT ||
         mode == MINI_DIGITAL_IO_MODE_OUTPUT_OPEN_DRAIN) &&
        gpio_set_level(gpio, initial_level) != ESP_OK) {
        (void)gpio_reset_pin(gpio);
        return MINI_ERR_IO;
    }

    *out_line = (minishell_backend_digital_t)((uintptr_t)line_id + 1u);
    return MINI_OK;
}

static mini_result_t digital_read(void *ctx, minishell_backend_digital_t line,
                                  uint32_t *out_level)
{
    (void)ctx;
    gpio_num_t gpio = handle_gpio(line);
    if (gpio == GPIO_NUM_NC) return MINI_ERR_BAD_HANDLE;
    if (out_level == NULL) return MINI_ERR_INVALID;
    *out_level = gpio_get_level(gpio) != 0 ? 1u : 0u;
    return MINI_OK;
}

static mini_result_t digital_write(void *ctx, minishell_backend_digital_t line,
                                   uint32_t level)
{
    (void)ctx;
    gpio_num_t gpio = handle_gpio(line);
    if (gpio == GPIO_NUM_NC) return MINI_ERR_BAD_HANDLE;
    if (level > 1u) return MINI_ERR_INVALID;
    return gpio_set_level(gpio, level) == ESP_OK ? MINI_OK : MINI_ERR_IO;
}

static mini_result_t digital_close(void *ctx, minishell_backend_digital_t line)
{
    (void)ctx;
    gpio_num_t gpio = handle_gpio(line);
    if (gpio == GPIO_NUM_NC) return MINI_ERR_BAD_HANDLE;
    return gpio_reset_pin(gpio) == ESP_OK ? MINI_OK : MINI_ERR_IO;
}

void adv_digital_io_configure(minishell_services_port_t *port)
{
    if (port == NULL) return;
    port->digital_io_capabilities = MINI_DIGITAL_IO_CAP_INPUT |
                                    MINI_DIGITAL_IO_CAP_INPUT_PULLUP |
                                    MINI_DIGITAL_IO_CAP_OUTPUT |
                                    MINI_DIGITAL_IO_CAP_OUTPUT_OPEN_DRAIN;
    port->digital_io_open = digital_open;
    port->digital_io_read = digital_read;
    port->digital_io_write = digital_write;
    port->digital_io_close = digital_close;
}

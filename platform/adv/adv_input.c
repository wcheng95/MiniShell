#include <stdint.h>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "adv_internal.h"
#include "minishell_services.h"

uint64_t adv_monotonic_us(void *ctx)
{
    (void)ctx;
    return (uint64_t)esp_timer_get_time();
}

mini_result_t adv_sleep_ms(void *ctx, uint32_t milliseconds)
{
    (void)ctx;
    if (milliseconds == 0u) {
        taskYIELD();
        return MINI_OK;
    }
    TickType_t ticks = pdMS_TO_TICKS(milliseconds);
    if (ticks == 0u) ticks = 1u;
    vTaskDelay(ticks);
    return MINI_OK;
}

mini_result_t adv_input_wait(void *ctx, uint32_t timeout_ms)
{
    (void)ctx;
    const uint64_t start = adv_monotonic_us(NULL);

    for (;;) {
        mini_key_event_t event = {.struct_size = sizeof(event)};
        mini_result_t result = adv_keyboard_read_event(&event);
        if (result == MINI_OK) {
            return minishell_services_input_submit(&event);
        }
        if (result != MINI_ERR_NOT_READY) return result;
        if (timeout_ms == MINI_WAIT_NONE) return MINI_ERR_NOT_READY;

        if (timeout_ms != MINI_WAIT_FOREVER) {
            const uint64_t elapsed = adv_monotonic_us(NULL) - start;
            if (elapsed >= (uint64_t)timeout_ms * 1000u) return MINI_ERR_TIMEOUT;
        }
        vTaskDelay(1u);
    }
}

void adv_input_flush(void *ctx)
{
    (void)ctx;
    adv_keyboard_flush();
}

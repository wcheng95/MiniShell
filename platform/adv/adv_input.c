#include <stdint.h>
#include <stdlib.h>
#include "esp_heap_caps.h"
#include "adv_ft8_web_io.h"

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "adv_internal.h"
#include "minishell_services.h"

typedef struct {
    mini_key_event_t events[ADV_REMOTE_KEYS];
    unsigned head, count;
} remote_keys_t;
static remote_keys_t *s_remote;
static portMUX_TYPE s_remote_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_remote_first;

bool adv_remote_input_start(void)
{
    remote_keys_t *keys = heap_caps_calloc(1, sizeof(*keys), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!keys) return false;
    portENTER_CRITICAL(&s_remote_lock);
    bool available = s_remote == NULL;
    if (available) { s_remote = keys; s_remote_first = false; }
    portEXIT_CRITICAL(&s_remote_lock);
    if (!available) free(keys);
    return available;
}

void adv_remote_input_stop(void)
{
    portENTER_CRITICAL(&s_remote_lock);
    remote_keys_t *keys = s_remote;
    s_remote = NULL;
    portEXIT_CRITICAL(&s_remote_lock);
    free(keys);
}

mini_result_t adv_remote_input_push(const mini_key_event_t *event)
{
    portENTER_CRITICAL(&s_remote_lock);
    mini_result_t result = MINI_ERR_NOT_READY;
    if (s_remote) {
        result = MINI_ERR_NO_SPACE;
        if (s_remote->count < ADV_REMOTE_KEYS) {
            s_remote->events[(s_remote->head + s_remote->count) % ADV_REMOTE_KEYS] = *event;
            ++s_remote->count;
            result = MINI_OK;
        }
    }
    portEXIT_CRITICAL(&s_remote_lock);
    return result;
}

static bool remote_pop(mini_key_event_t *event)
{
    portENTER_CRITICAL(&s_remote_lock);
    bool present = s_remote && s_remote->count;
    if (present) {
        *event = s_remote->events[s_remote->head];
        s_remote->head = (s_remote->head + 1) % ADV_REMOTE_KEYS;
        --s_remote->count;
    }
    portEXIT_CRITICAL(&s_remote_lock);
    return present;
}

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
        if (s_remote_first && remote_pop(&event)) {
            s_remote_first = false;
            return minishell_services_input_submit(&event);
        }
        mini_result_t result = adv_keyboard_read_event(&event);
        if (result == MINI_OK) {
            s_remote_first = true;
            return minishell_services_input_submit(&event);
        }
        if (result != MINI_ERR_NOT_READY) return result;
        if (remote_pop(&event)) {
            s_remote_first = false;
            return minishell_services_input_submit(&event);
        }
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
    portENTER_CRITICAL(&s_remote_lock);
    if (s_remote) s_remote->head = s_remote->count = 0;
    portEXIT_CRITICAL(&s_remote_lock);
}

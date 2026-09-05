#include <string.h>

#include "services_internal.h"

#define MINI_INPUT_QUEUE_CAPACITY 64u

typedef struct {
    mini_key_event_t events[MINI_INPUT_QUEUE_CAPACITY];
    uint32_t head;
    uint32_t count;
} input_queue_t;

static input_queue_t s_queue;
static bool s_available;
static mini_key_input_api_t s_key_api;
static mini_input_api_t s_input_api;

static void input_lock(void)
{
    const minishell_services_port_t *port = minishell_services_port();
    if (port->input_lock != NULL) port->input_lock(port->ctx);
}

static void input_unlock(void)
{
    const minishell_services_port_t *port = minishell_services_port();
    if (port->input_unlock != NULL) port->input_unlock(port->ctx);
}

static bool pop_event(mini_key_event_t *out_event)
{
    bool found = false;
    input_lock();
    if (s_queue.count > 0u) {
        *out_event = s_queue.events[s_queue.head];
        s_queue.head = (s_queue.head + 1u) % MINI_INPUT_QUEUE_CAPACITY;
        --s_queue.count;
        found = true;
    }
    input_unlock();
    return found;
}

void minishell_services_input_flush(void)
{
    input_lock();
    memset(&s_queue, 0, sizeof(s_queue));
    input_unlock();
}

static bool valid_unicode_scalar(uint32_t codepoint)
{
    return codepoint <= 0x10FFFFu && !(codepoint >= 0xD800u && codepoint <= 0xDFFFu);
}

mini_result_t minishell_services_input_submit(const mini_key_event_t *event)
{
    const minishell_services_port_t *port = minishell_services_port();
    const uint32_t v0_size = MINI_FIELD_END(mini_key_event_t, modifiers);
    if (!s_available || (s_input_api.capabilities & MINI_INPUT_CAP_KEY) == 0u) return MINI_ERR_UNSUPPORTED;
    if (event == NULL || event->struct_size < v0_size) return MINI_ERR_INVALID;
    if (event->type == MINI_KEY_EVENT_CHAR) {
        if (!valid_unicode_scalar(event->codepoint) || event->key != 0u) return MINI_ERR_INVALID;
    } else if (event->type == MINI_KEY_EVENT_SPECIAL) {
        if (event->codepoint != 0u || event->key == 0u) return MINI_ERR_INVALID;
    } else if (event->type == 0u) {
        return MINI_ERR_INVALID;
    }
    input_lock();
    if (s_queue.count >= MINI_INPUT_QUEUE_CAPACITY) {
        input_unlock();
        return MINI_ERR_NO_SPACE;
    }
    uint32_t tail = (s_queue.head + s_queue.count) % MINI_INPUT_QUEUE_CAPACITY;
    s_queue.events[tail] = *event;
    s_queue.events[tail].struct_size = sizeof(mini_key_event_t);
    ++s_queue.count;
    input_unlock();
    if (port->input_wake != NULL) port->input_wake(port->ctx);
    return MINI_OK;
}

static mini_result_t input_read(mini_key_event_t *out_event, uint32_t timeout_ms)
{
    const minishell_services_port_t *port = minishell_services_port();
    const uint32_t v0_size = MINI_FIELD_END(mini_key_event_t, modifiers);
    if ((s_input_api.capabilities & MINI_INPUT_CAP_KEY) == 0u) return MINI_ERR_UNSUPPORTED;
    if (out_event == NULL || out_event->struct_size < v0_size) return MINI_ERR_INVALID;
    uint32_t caller_size = out_event->struct_size;
    mini_key_event_t event;
    if (pop_event(&event)) {
        *out_event = event;
        out_event->struct_size = caller_size;
        return MINI_OK;
    }
    if (timeout_ms == MINI_WAIT_NONE) return MINI_ERR_NOT_READY;
    uint64_t start = port->monotonic_us(port->ctx);
    for (;;) {
        uint32_t wait_ms = timeout_ms;
        if (timeout_ms != MINI_WAIT_FOREVER) {
            uint64_t elapsed_us = port->monotonic_us(port->ctx) - start;
            uint64_t timeout_us = (uint64_t)timeout_ms * 1000u;
            if (elapsed_us >= timeout_us) return MINI_ERR_TIMEOUT;
            uint64_t remaining_us = timeout_us - elapsed_us;
            wait_ms = (uint32_t)((remaining_us + 999u) / 1000u);
            if (wait_ms == 0u) wait_ms = 1u;
        }
        mini_result_t wait_result = port->input_wait(port->ctx, wait_ms);
        if (pop_event(&event)) {
            *out_event = event;
            out_event->struct_size = caller_size;
            return MINI_OK;
        }
        if (timeout_ms != MINI_WAIT_FOREVER) {
            uint64_t elapsed_us = port->monotonic_us(port->ctx) - start;
            if (elapsed_us >= (uint64_t)timeout_ms * 1000u) return MINI_ERR_TIMEOUT;
        }
        if (wait_result != MINI_OK && wait_result != MINI_ERR_TIMEOUT && wait_result != MINI_ERR_NOT_READY) return wait_result;
        if (timeout_ms == MINI_WAIT_FOREVER && wait_result == MINI_ERR_UNSUPPORTED) return MINI_ERR_UNSUPPORTED;
    }
}

void minishell_input_service_configure(void)
{
    const minishell_services_port_t *port = minishell_services_port();
    memset(&s_queue, 0, sizeof(s_queue));
    s_key_api.struct_size = sizeof(s_key_api);
    s_key_api.read = input_read;
    s_input_api.struct_size = sizeof(s_input_api);
    s_input_api.capabilities = 0u;
    s_input_api.key = NULL;
    s_available = false;
    bool key_ready = (port->input_capabilities & MINI_INPUT_CAP_KEY) != 0u && port->input_wait != NULL && port->monotonic_us != NULL;
    if (key_ready) {
        s_available = true;
        s_input_api.capabilities = MINI_INPUT_CAP_KEY;
        s_input_api.key = &s_key_api;
    }
}

void minishell_input_service_app_begin(void) { minishell_services_input_flush(); }
void minishell_input_service_app_end(void) { minishell_services_input_flush(); }
bool minishell_input_service_available(void) { return s_available; }
const mini_input_api_t *minishell_input_service_api(void) { return &s_input_api; }

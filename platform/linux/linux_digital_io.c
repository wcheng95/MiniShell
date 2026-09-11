#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "linux_internal.h"

#define LINUX_DIGITAL_MAX_OPEN 16u

typedef struct {
    bool open;
    uint32_t line_id;
    uint32_t mode;
    uint32_t level;
} linux_digital_line_t;

static linux_digital_line_t s_lines[LINUX_DIGITAL_MAX_OPEN];

static linux_digital_line_t *from_handle(minishell_backend_digital_t handle)
{
    if (handle == MINISHELL_BACKEND_DIGITAL_INVALID) return NULL;
    uintptr_t index = handle - 1u;
    if (index >= LINUX_DIGITAL_MAX_OPEN || !s_lines[index].open) return NULL;
    return &s_lines[index];
}

static mini_result_t digital_open(void *ctx, uint32_t line_id, uint32_t mode,
                                  uint32_t initial_level,
                                  minishell_backend_digital_t *out_line)
{
    (void)ctx;
    if (out_line == NULL || initial_level > 1u) return MINI_ERR_INVALID;
    *out_line = MINISHELL_BACKEND_DIGITAL_INVALID;

    for (size_t i = 0u; i < LINUX_DIGITAL_MAX_OPEN; ++i) {
        if (s_lines[i].open && s_lines[i].line_id == line_id) return MINI_ERR_EXISTS;
    }

    for (size_t i = 0u; i < LINUX_DIGITAL_MAX_OPEN; ++i) {
        if (s_lines[i].open) continue;
        s_lines[i].open = true;
        s_lines[i].line_id = line_id;
        s_lines[i].mode = mode;
        if (mode == MINI_DIGITAL_IO_MODE_INPUT_PULLUP) s_lines[i].level = 1u;
        else if (mode == MINI_DIGITAL_IO_MODE_INPUT) s_lines[i].level = 0u;
        else s_lines[i].level = initial_level;
        *out_line = (minishell_backend_digital_t)(i + 1u);
        return MINI_OK;
    }
    return MINI_ERR_TOO_MANY_OPEN;
}

static mini_result_t digital_read(void *ctx, minishell_backend_digital_t line,
                                  uint32_t *out_level)
{
    (void)ctx;
    linux_digital_line_t *state = from_handle(line);
    if (state == NULL) return MINI_ERR_BAD_HANDLE;
    if (out_level == NULL) return MINI_ERR_INVALID;
    *out_level = state->level;
    return MINI_OK;
}

static mini_result_t digital_write(void *ctx, minishell_backend_digital_t line,
                                   uint32_t level)
{
    (void)ctx;
    linux_digital_line_t *state = from_handle(line);
    if (state == NULL) return MINI_ERR_BAD_HANDLE;
    if (level > 1u) return MINI_ERR_INVALID;
    if (state->mode != MINI_DIGITAL_IO_MODE_OUTPUT &&
        state->mode != MINI_DIGITAL_IO_MODE_OUTPUT_OPEN_DRAIN) {
        return MINI_ERR_ACCESS;
    }
    state->level = level;
    return MINI_OK;
}

static mini_result_t digital_close(void *ctx, minishell_backend_digital_t line)
{
    (void)ctx;
    linux_digital_line_t *state = from_handle(line);
    if (state == NULL) return MINI_ERR_BAD_HANDLE;
    memset(state, 0, sizeof(*state));
    return MINI_OK;
}

void linux_digital_io_configure(minishell_services_port_t *port)
{
    if (port == NULL) return;
    memset(s_lines, 0, sizeof(s_lines));
    port->digital_io_capabilities = MINI_DIGITAL_IO_CAP_INPUT |
                                    MINI_DIGITAL_IO_CAP_INPUT_PULLUP |
                                    MINI_DIGITAL_IO_CAP_OUTPUT |
                                    MINI_DIGITAL_IO_CAP_OUTPUT_OPEN_DRAIN;
    port->digital_io_open = digital_open;
    port->digital_io_read = digital_read;
    port->digital_io_write = digital_write;
    port->digital_io_close = digital_close;
}

#include <string.h>

#include "services_internal.h"

#define MINI_DIGITAL_IO_MAX_OPEN 16u

typedef struct {
    bool open;
    mini_digital_io_t public_handle;
    minishell_backend_digital_t backend_handle;
    uint32_t line_id;
    uint32_t mode;
} digital_line_state_t;

static bool s_available;
static mini_digital_io_api_t s_api;
static digital_line_state_t s_lines[MINI_DIGITAL_IO_MAX_OPEN];
static mini_digital_io_t s_next_handle = 1u;

static uint64_t mode_capability(uint32_t mode)
{
    switch (mode) {
    case MINI_DIGITAL_IO_MODE_INPUT:
        return MINI_DIGITAL_IO_CAP_INPUT;
    case MINI_DIGITAL_IO_MODE_INPUT_PULLUP:
        return MINI_DIGITAL_IO_CAP_INPUT_PULLUP;
    case MINI_DIGITAL_IO_MODE_OUTPUT:
        return MINI_DIGITAL_IO_CAP_OUTPUT;
    case MINI_DIGITAL_IO_MODE_OUTPUT_OPEN_DRAIN:
        return MINI_DIGITAL_IO_CAP_OUTPUT_OPEN_DRAIN;
    default:
        return 0u;
    }
}

static bool mode_is_output(uint32_t mode)
{
    return mode == MINI_DIGITAL_IO_MODE_OUTPUT ||
           mode == MINI_DIGITAL_IO_MODE_OUTPUT_OPEN_DRAIN;
}

static digital_line_state_t *find_handle(mini_digital_io_t handle)
{
    if (handle == MINI_DIGITAL_IO_INVALID) return NULL;
    for (size_t i = 0u; i < MINI_DIGITAL_IO_MAX_OPEN; ++i) {
        if (s_lines[i].open && s_lines[i].public_handle == handle) return &s_lines[i];
    }
    return NULL;
}

static bool line_is_open(uint32_t line_id)
{
    for (size_t i = 0u; i < MINI_DIGITAL_IO_MAX_OPEN; ++i) {
        if (s_lines[i].open && s_lines[i].line_id == line_id) return true;
    }
    return false;
}

static digital_line_state_t *find_free_slot(void)
{
    for (size_t i = 0u; i < MINI_DIGITAL_IO_MAX_OPEN; ++i) {
        if (!s_lines[i].open) return &s_lines[i];
    }
    return NULL;
}

static mini_digital_io_t allocate_public_handle(void)
{
    for (;;) {
        mini_digital_io_t handle = s_next_handle++;
        if (s_next_handle == MINI_DIGITAL_IO_INVALID) s_next_handle = 1u;
        if (handle == MINI_DIGITAL_IO_INVALID) continue;
        if (find_handle(handle) == NULL) return handle;
    }
}

static mini_result_t digital_open(const mini_digital_io_config_t *config,
                                  mini_digital_io_t *out_line)
{
    const minishell_services_port_t *port = minishell_services_port();
    const uint32_t required_size = MINI_FIELD_END(mini_digital_io_config_t, initial_level);

    if (out_line == NULL) return MINI_ERR_INVALID;
    *out_line = MINI_DIGITAL_IO_INVALID;
    if (config == NULL || config->struct_size < required_size || config->initial_level > 1u) {
        return MINI_ERR_INVALID;
    }

    uint64_t capability = mode_capability(config->mode);
    if (capability == 0u) return MINI_ERR_INVALID;
    if ((s_api.capabilities & capability) == 0u) return MINI_ERR_UNSUPPORTED;
    if (line_is_open(config->line_id)) return MINI_ERR_EXISTS;

    digital_line_state_t *slot = find_free_slot();
    if (slot == NULL) return MINI_ERR_TOO_MANY_OPEN;

    minishell_backend_digital_t backend = MINISHELL_BACKEND_DIGITAL_INVALID;
    mini_result_t result = port->digital_io_open(port->ctx, config->line_id,
                                                  config->mode, config->initial_level,
                                                  &backend);
    if (result != MINI_OK) return result;
    if (backend == MINISHELL_BACKEND_DIGITAL_INVALID) return MINI_ERR_IO;

    memset(slot, 0, sizeof(*slot));
    slot->open = true;
    slot->public_handle = allocate_public_handle();
    slot->backend_handle = backend;
    slot->line_id = config->line_id;
    slot->mode = config->mode;
    *out_line = slot->public_handle;
    return MINI_OK;
}

static mini_result_t digital_read(mini_digital_io_t line, uint32_t *out_level)
{
    const minishell_services_port_t *port = minishell_services_port();
    digital_line_state_t *state = find_handle(line);
    if (state == NULL) return MINI_ERR_BAD_HANDLE;
    if (out_level == NULL) return MINI_ERR_INVALID;
    *out_level = 0u;

    mini_result_t result = port->digital_io_read(port->ctx, state->backend_handle,
                                                  out_level);
    if (result == MINI_OK && *out_level > 1u) {
        *out_level = 0u;
        return MINI_ERR_IO;
    }
    return result;
}

static mini_result_t digital_write(mini_digital_io_t line, uint32_t level)
{
    const minishell_services_port_t *port = minishell_services_port();
    digital_line_state_t *state = find_handle(line);
    if (state == NULL) return MINI_ERR_BAD_HANDLE;
    if (level > 1u) return MINI_ERR_INVALID;
    if (!mode_is_output(state->mode)) return MINI_ERR_ACCESS;
    return port->digital_io_write(port->ctx, state->backend_handle, level);
}

static mini_result_t digital_close(mini_digital_io_t line)
{
    const minishell_services_port_t *port = minishell_services_port();
    digital_line_state_t *state = find_handle(line);
    if (state == NULL) return MINI_ERR_BAD_HANDLE;

    mini_result_t result = port->digital_io_close(port->ctx, state->backend_handle);
    if (result == MINI_OK) memset(state, 0, sizeof(*state));
    return result;
}

void minishell_digital_io_service_configure(void)
{
    const minishell_services_port_t *port = minishell_services_port();
    memset(s_lines, 0, sizeof(s_lines));
    memset(&s_api, 0, sizeof(s_api));

    s_api.struct_size = sizeof(s_api);
    s_api.capabilities = port->digital_io_capabilities &
                         (MINI_DIGITAL_IO_CAP_INPUT |
                          MINI_DIGITAL_IO_CAP_INPUT_PULLUP |
                          MINI_DIGITAL_IO_CAP_OUTPUT |
                          MINI_DIGITAL_IO_CAP_OUTPUT_OPEN_DRAIN);
    s_api.open = digital_open;
    s_api.read = digital_read;
    s_api.write = digital_write;
    s_api.close = digital_close;

    s_available = s_api.capabilities != 0u &&
                  port->digital_io_open != NULL &&
                  port->digital_io_read != NULL &&
                  port->digital_io_write != NULL &&
                  port->digital_io_close != NULL;
    if (!s_available) s_api.capabilities = 0u;
}

void minishell_digital_io_service_app_begin(void)
{
    /* Lines are app-owned. A previous app is cleaned at app_end(). */
}

void minishell_digital_io_service_app_end(void)
{
    const minishell_services_port_t *port = minishell_services_port();
    for (size_t i = 0u; i < MINI_DIGITAL_IO_MAX_OPEN; ++i) {
        if (!s_lines[i].open) continue;
        if (port->digital_io_close != NULL) {
            (void)port->digital_io_close(port->ctx, s_lines[i].backend_handle);
        }
        memset(&s_lines[i], 0, sizeof(s_lines[i]));
    }
}

bool minishell_digital_io_service_available(void) { return s_available; }
const mini_digital_io_api_t *minishell_digital_io_service_api(void) { return &s_api; }

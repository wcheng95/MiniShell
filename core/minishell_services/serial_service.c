#include "services_internal.h"

static mini_serial_api_t s_api;
static minishell_backend_serial_t s_backend;
static mini_serial_t s_handle, s_next_handle = 1u;

static mini_result_t serial_open(const char *endpoint, mini_serial_t *out)
{
    const minishell_services_port_t *port = minishell_services_port();
    if (!out) return MINI_ERR_INVALID;
    *out = MINI_SERIAL_INVALID;
    if (!endpoint || !*endpoint) return MINI_ERR_INVALID;
    if (!s_api.capabilities) return MINI_ERR_UNSUPPORTED;
    if (s_handle) return MINI_ERR_TOO_MANY_OPEN;
    minishell_backend_serial_t backend = MINISHELL_BACKEND_SERIAL_INVALID;
    mini_result_t result = port->serial_open(port->ctx, endpoint, &backend);
    if (result != MINI_OK) return result;
    if (!backend) return MINI_ERR_IO;
    s_backend = backend;
    s_handle = s_next_handle++;
    if (!s_next_handle) s_next_handle = 1u;
    *out = s_handle;
    return MINI_OK;
}

static mini_result_t transfer(mini_serial_t serial, void *read_buffer,
                               const void *write_buffer, uint32_t size,
                               uint32_t *out, uint32_t timeout, bool writing)
{
    const minishell_services_port_t *port = minishell_services_port();
    if (!out) return MINI_ERR_INVALID;
    *out = 0;
    if (!serial || serial != s_handle) return MINI_ERR_BAD_HANDLE;
    uint64_t cap = writing ? MINI_SERIAL_CAP_WRITE : MINI_SERIAL_CAP_READ;
    if (!(s_api.capabilities & cap)) return MINI_ERR_UNSUPPORTED;
    if (!size) return MINI_OK;
    if (writing ? !write_buffer : !read_buffer) return MINI_ERR_INVALID;
    mini_result_t result = writing
        ? port->serial_write(port->ctx, s_backend, write_buffer, size, out, timeout)
        : port->serial_read(port->ctx, s_backend, read_buffer, size, out, timeout);
    if (*out > size) { *out = 0; return MINI_ERR_IO; }
    if (result != MINI_OK) *out = 0;
    return result;
}

static mini_result_t serial_read(mini_serial_t serial, void *buffer, uint32_t size,
                                 uint32_t *out, uint32_t timeout)
{ return transfer(serial, buffer, NULL, size, out, timeout, false); }

static mini_result_t serial_write(mini_serial_t serial, const void *buffer, uint32_t size,
                                  uint32_t *out, uint32_t timeout)
{ return transfer(serial, NULL, buffer, size, out, timeout, true); }

static mini_result_t serial_close(mini_serial_t serial)
{
    if (!serial || serial != s_handle) return MINI_ERR_BAD_HANDLE;
    const minishell_services_port_t *port = minishell_services_port();
    mini_result_t result = port->serial_close(port->ctx, s_backend);
    s_handle = MINI_SERIAL_INVALID;
    s_backend = MINISHELL_BACKEND_SERIAL_INVALID;
    return result;
}

void minishell_serial_service_app_end(void)
{
    if (s_handle) (void)serial_close(s_handle);
}

void minishell_serial_service_configure(void)
{
    const minishell_services_port_t *port = minishell_services_port();
    s_api = (mini_serial_api_t){.struct_size = sizeof(s_api), .open = serial_open,
        .read = serial_read, .write = serial_write, .close = serial_close};
    if (!port->serial_open || !port->serial_close) return;
    if (port->serial_read) s_api.capabilities |= port->serial_capabilities & MINI_SERIAL_CAP_READ;
    if (port->serial_write) s_api.capabilities |= port->serial_capabilities & MINI_SERIAL_CAP_WRITE;
}

bool minishell_serial_service_available(void) { return s_api.capabilities != 0; }
const mini_serial_api_t *minishell_serial_service_api(void) { return &s_api; }

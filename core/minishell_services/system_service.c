#include "services_internal.h"

static void system_write(const char *text)
{
    const minishell_services_port_t *port = minishell_services_port();
    if (text == NULL) {
        return;
    }
    if (port->system_write != NULL) {
        port->system_write(port->ctx, text);
    }
}

static const mini_system_api_t s_system_api = {
    .struct_size = sizeof(mini_system_api_t),
    .write = system_write,
};

const mini_system_api_t *minishell_system_service_api(void)
{
    return &s_system_api;
}

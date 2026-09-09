#include "services_internal.h"

static void console_write(const char *text)
{
    const minishell_services_port_t *port = minishell_services_port();
    if (text == NULL) return;
    if (port->console_write != NULL) {
        port->console_write(port->ctx, text);
    }
}

static const mini_console_api_t s_console_api = {
    .struct_size = sizeof(mini_console_api_t),
    .write = console_write,
};

const mini_console_api_t *minishell_console_service_api(void)
{
    return &s_console_api;
}

bool minishell_console_service_available(void)
{
    const minishell_services_port_t *port = minishell_services_port();
    return port->console_write != NULL;
}

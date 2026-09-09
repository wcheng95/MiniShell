#include <string.h>

#include "adv_internal.h"
#include "minishell_services.h"
#include "platform_backend.h"

static minishell_services_port_t s_services_port;

static void system_write(void *ctx, const char *text)
{
    (void)ctx;
    minishell_platform_console_write(text);
}

static void configure_services_port(void)
{
    memset(&s_services_port, 0, sizeof(s_services_port));
    s_services_port.system_write = system_write;
}

int minishell_platform_init(void)
{
    if (adv_console_prepare() != 0) return -1;
    configure_services_port();
    return 0;
}

void minishell_platform_shutdown(void)
{
}

const minishell_services_port_t *minishell_platform_services_port(void)
{
    return &s_services_port;
}

void minishell_platform_services_prepare(minishell_services_port_t *out_port)
{
    if (out_port == NULL) return;
    memset(out_port, 0, sizeof(*out_port));
    *out_port = s_services_port;
}

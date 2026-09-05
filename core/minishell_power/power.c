#include "minishell_power.h"

#include <string.h>

static minishell_power_port_t s_port;
static bool s_configured;

void minishell_power_configure(const minishell_power_port_t *port)
{
    memset(&s_port, 0, sizeof(s_port));
    if (port != NULL) s_port = *port;
    s_configured = port != NULL;
}

int minishell_power_get_status(minishell_power_status_t *out_status)
{
    if (out_status == NULL) return -1;

    memset(out_status, 0, sizeof(*out_status));
    if (!s_configured || s_port.get_status == NULL) return -1;

    int result = s_port.get_status(s_port.ctx, out_status);
    if (result != 0) {
        memset(out_status, 0, sizeof(*out_status));
        return result;
    }

    if (out_status->battery_percent_valid && out_status->battery_percent > 100u) {
        out_status->battery_percent = 100u;
    }
    return 0;
}

int minishell_power_suspend(void)
{
    if (!s_configured || s_port.suspend == NULL) return -1;
    return s_port.suspend(s_port.ctx);
}

int minishell_power_poweroff(void)
{
    if (!s_configured || s_port.poweroff == NULL) return -1;
    return s_port.poweroff(s_port.ctx);
}

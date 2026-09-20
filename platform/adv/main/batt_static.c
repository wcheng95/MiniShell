#include <stdio.h>

#include "adv_internal.h"
#include "minishell/api.h"

int minishell_app_batt_main(int argc, char **argv)
{
    (void)argv;
    const mini_api_t *api = mini_api_get();
    if (argc != 1) return 2;
    if (api == NULL || api->console == NULL || api->console->write == NULL)
        return 2;

    int voltage_mv = -1;
    int percent = -1;
    if (adv_battery_read(&voltage_mv, &percent) != MINI_OK) {
        api->console->write("batt: unavailable\n");
        return 1;
    }

    char line[48];
    (void)snprintf(line, sizeof(line), "battery %d%% %dmV\n", percent, voltage_mv);
    api->console->write(line);
    return 0;
}

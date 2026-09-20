#include "adv_internal.h"
#include "minishell/api.h"

int minishell_app_sleep_main(int argc, char **argv)
{
    (void)argv;
    const mini_api_t *api = mini_api_get();
    if (argc != 1) return 2;
    if (api == NULL || api->console == NULL || api->console->write == NULL)
        return 2;

    api->console->write("sleep: GPIO0 wakes ADV\n");
    if (adv_enter_deep_sleep() != MINI_OK) {
        api->console->write("sleep: failed\n");
        return 1;
    }
    return 0;
}

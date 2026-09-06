#include <stddef.h>

#include "minishell/api.h"

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    const mini_api_t *api = mini_api_get();
    if (api == NULL ||
        api->abi_version != MINISHELL_ABI_VERSION ||
        api->system == NULL ||
        api->system->write == NULL) {
        return 2;
    }

    api->system->write("Hello from MiniShell.\n");
    return 0;
}

#include <stddef.h>

#include "minishell/api.h"

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    const mini_api_t *api = mini_api_get();

    if (api == NULL) {
        return 2;
    }

    if (api->abi_version != MINISHELL_ABI_VERSION ||
        api->struct_size < sizeof(mini_api_t) ||
        api->system == NULL ||
        api->system->struct_size < sizeof(mini_system_api_t) ||
        api->system->write == NULL) {
        return 3;
    }

    api->system->write("Hello from a MiniShell ELF app.\n");
    return 0;
}

#include "minishell/api.h"

int main(int argc, char **argv)
{
    const mini_api_t *api;

    (void)argc;
    (void)argv;

    api = mini_api_get();
    if (api == NULL || api->api_version != MINISHELL_API_VERSION ||
        api->console == NULL || api->console->write == NULL) {
        return 2;
    }

    api->console->write("elfhello: MiniShell API OK\n");
    return 0;
}

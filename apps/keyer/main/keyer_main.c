#include "app_controller.h"
#include "minishell/api.h"

int main(int argc, char **argv)
{
    const mini_api_t *api;
    mini_result_t rc;
    int result;

    (void)argc;
    (void)argv;

    api = mini_api_get();
    if (api == 0 || api->api_version != MINISHELL_API_VERSION) return 2;

    rc = app_controller_init(api);
    if (rc != MINI_OK) return 3;

    result = app_controller_run();
    app_controller_shutdown();
    return result;
}

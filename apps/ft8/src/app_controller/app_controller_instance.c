#include "app_controller_internal.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

AppController *app_controller_create(const mini_api_t *api,
                                     const char *data_directory,
                                     const char *station_path)
{
    AppController *app = NULL;

    if (api == NULL || api->memory == NULL || api->memory->alloc == NULL ||
        api->memory->free == NULL || data_directory == NULL || station_path == NULL ||
        sizeof(*app) > UINT32_MAX) {
        return NULL;
    }

    if (api->memory->alloc((uint32_t)sizeof(*app), (void **)&app) != MINI_OK || app == NULL) {
        return NULL;
    }
    memset(app, 0, sizeof(*app));

    if (!app_controller_init(app, api, data_directory, station_path)) {
        (void)api->memory->free(app);
        return NULL;
    }
    return app;
}

void app_controller_destroy(AppController *app)
{
    const mini_memory_api_t *memory;

    if (app == NULL) return;
    memory = app->api != NULL ? app->api->memory : NULL;
    app_controller_shutdown(app);
    if (memory != NULL && memory->free != NULL) {
        (void)memory->free(app);
    }
}

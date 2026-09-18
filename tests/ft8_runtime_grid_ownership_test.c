#define FT8_APP_CONTROLLER_INTERNAL 1
#include "app_controller_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr) do { if (!(expr)) {     fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); exit(1); } } while (0)

static mini_location_t s_location;
static int s_have_location;
static unsigned s_station_updates;
static char s_last_grid[FT8_CONFIG_GRID_CAP];

static mini_result_t fake_location_get(mini_location_t *out_location)
{
    if (!s_have_location) return MINI_ERR_NOT_READY;
    *out_location = s_location;
    return MINI_OK;
}

/* app_controller_instance.c is under test; unrelated controller entry points are stubs. */
bool app_controller_init(AppController *app, const mini_api_t *api,
                         const char *data_directory, const char *station_path)
{
    (void)app; (void)api; (void)data_directory; (void)station_path;
    return false;
}

void app_controller_shutdown(AppController *app)
{
    (void)app;
}

void app_controller_build_ui_model(const AppController *app, UiModel *model)
{
    (void)app;
    memset(model, 0, sizeof(*model));
}

void app_controller_build_memory_model(const AppController *app, UiModel *model)
{
    (void)app;
    memset(model, 0, sizeof(*model));
}

bool auto_seq_set_station(AutoSeq *seq, const char *callsign, const char *grid)
{
    (void)seq;
    CHECK(strcmp(callsign, "AG6AQ") == 0);
    CHECK(strlen(grid) < sizeof(s_last_grid));
    strcpy(s_last_grid, grid);
    ++s_station_updates;
    return true;
}

int main(void)
{
    mini_time_location_api_t time_location = {
        .struct_size = sizeof(time_location),
        .capabilities = MINI_TIMELOC_CAP_LOCATION,
        .location_get = fake_location_get,
    };
    mini_api_t api = {
        .api_version = MINISHELL_API_VERSION,
        .struct_size = sizeof(api),
        .time_location = &time_location,
    };
    AppController app;
    memset(&app, 0, sizeof(app));
    app.api = &api;
    strcpy(app.config.callsign, "AG6AQ");
    strcpy(app.config.grid, "CM87");
    strcpy(app.effective_grid, app.config.grid);

    memset(&s_location, 0, sizeof(s_location));
    s_location.struct_size = sizeof(s_location);
    s_location.latitude_e7 = 373382000;
    s_location.longitude_e7 = -1218863000;
    s_location.source = MINI_LOCATION_SOURCE_LIVE;
    s_location.updated_monotonic_us = 1000u;
    s_have_location = 1;

    bool changed = false;
    CHECK(app_controller_step_location(&app, &changed));
    CHECK(changed);
    CHECK(app.gps_grid_active);
    CHECK(strcmp(app.config.grid, "CM87") == 0);
    CHECK(strcmp(app.effective_grid, "CM97") == 0);
    CHECK(strcmp(s_last_grid, "CM97") == 0);
    CHECK(s_station_updates == 1u);

    char serialized[512];
    CHECK(config_service_serialize(&app.config, serialized, sizeof(serialized)));
    CHECK(strstr(serialized, "grid=CM87\n") != NULL);
    CHECK(strstr(serialized, "grid=CM97\n") == NULL);

    changed = true;
    CHECK(app_controller_step_location(&app, &changed));
    CHECK(!changed);
    CHECK(s_station_updates == 1u);

    s_have_location = 0;
    changed = false;
    CHECK(app_controller_step_location(&app, &changed));
    CHECK(changed);
    CHECK(!app.gps_grid_active);
    CHECK(strcmp(app.config.grid, "CM87") == 0);
    CHECK(strcmp(app.effective_grid, "CM87") == 0);
    CHECK(strcmp(s_last_grid, "CM87") == 0);
    CHECK(s_station_updates == 2u);

    puts("ft8_runtime_grid_ownership_test: PASS");
    return 0;
}

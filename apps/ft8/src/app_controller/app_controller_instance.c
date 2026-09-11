#define FT8_APP_CONTROLLER_INTERNAL 1
#include "app_controller_internal.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

static void copy_grid(char out[FT8_CONFIG_GRID_CAP], const char *grid)
{
    size_t length = 0u;

    if (out == NULL) return;
    if (grid != NULL) {
        while (length + 1u < FT8_CONFIG_GRID_CAP && grid[length] != '\0') ++length;
        memcpy(out, grid, length);
    }
    out[length] = '\0';
}

static bool location_to_grid4(const mini_location_t *location,
                              char out[FT8_CONFIG_GRID_CAP])
{
    int64_t longitude;
    int64_t latitude;
    int64_t shifted_longitude;
    int64_t shifted_latitude;
    int field_longitude;
    int field_latitude;
    int square_longitude;
    int square_latitude;

    if (location == NULL || out == NULL) return false;
    longitude = location->longitude_e7;
    latitude = location->latitude_e7;
    if (longitude < -1800000000ll || longitude > 1800000000ll ||
        latitude < -900000000ll || latitude > 900000000ll) {
        return false;
    }

    shifted_longitude = longitude + 1800000000ll;
    shifted_latitude = latitude + 900000000ll;

    /* +180/+90 are closed numeric bounds but lie on the open upper edge of
     * the Maidenhead world grid. Clamp them into the final legal cell. */
    if (shifted_longitude >= 3600000000ll) shifted_longitude = 3599999999ll;
    if (shifted_latitude >= 1800000000ll) shifted_latitude = 1799999999ll;

    field_longitude = (int)(shifted_longitude / 200000000ll);
    field_latitude = (int)(shifted_latitude / 100000000ll);
    square_longitude = (int)((shifted_longitude % 200000000ll) / 20000000ll);
    square_latitude = (int)((shifted_latitude % 100000000ll) / 10000000ll);

    if (field_longitude < 0 || field_longitude > 17 ||
        field_latitude < 0 || field_latitude > 17 ||
        square_longitude < 0 || square_longitude > 9 ||
        square_latitude < 0 || square_latitude > 9) {
        return false;
    }

    out[0] = (char)('A' + field_longitude);
    out[1] = (char)('A' + field_latitude);
    out[2] = (char)('0' + square_longitude);
    out[3] = (char)('0' + square_latitude);
    out[4] = '\0';
    return true;
}

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

    /* Preserve the persistent station grid separately. A live GPS grid is a
     * session/runtime override and must never rewrite station.txt. */
    copy_grid(app->manual_grid, app->config.grid);
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

bool app_controller_step_location(AppController *app, bool *out_model_changed)
{
    const mini_time_location_api_t *time_location;
    mini_location_t location;
    char live_grid[FT8_CONFIG_GRID_CAP];
    bool have_live_grid = false;

    if (out_model_changed != NULL) *out_model_changed = false;
    if (app == NULL || app->api == NULL) return false;

    time_location = app->api->time_location;
    if (time_location != NULL &&
        (time_location->capabilities & MINI_TIMELOC_CAP_LOCATION) != 0u &&
        time_location->location_get != NULL) {
        memset(&location, 0, sizeof(location));
        location.struct_size = sizeof(location);
        if (time_location->location_get(&location) == MINI_OK &&
            location.source == MINI_LOCATION_SOURCE_LIVE &&
            location_to_grid4(&location, live_grid)) {
            have_live_grid = true;

            /* Avoid reapplying the same GPS fix merely because the main loop
             * runs faster than the NMEA source. */
            if (!app->gps_grid_active ||
                location.updated_monotonic_us != app->last_live_location_update_us ||
                strcmp(app->config.grid, live_grid) != 0) {
                if (strcmp(app->config.grid, live_grid) != 0) {
                    copy_grid(app->config.grid, live_grid);
                    if (!auto_seq_set_station(&app->auto_seq,
                                              app->config.callsign,
                                              app->config.grid)) {
                        return false;
                    }
                    if (out_model_changed != NULL) *out_model_changed = true;
                }
                app->last_live_location_update_us = location.updated_monotonic_us;
                app->gps_grid_active = true;
            }
        }
    }

    if (!have_live_grid && app->gps_grid_active) {
        copy_grid(app->config.grid, app->manual_grid);
        if (!auto_seq_set_station(&app->auto_seq,
                                  app->config.callsign,
                                  app->config.grid)) {
            return false;
        }
        app->gps_grid_active = false;
        app->last_live_location_update_us = 0u;
        if (out_model_changed != NULL) *out_model_changed = true;
    }

    return true;
}

void app_controller_build_model(const AppController *app, UiModel *model)
{
    if (model == NULL) return;
    if (app == NULL) {
        memset(model, 0, sizeof(*model));
        return;
    }

    app_controller_build_ui_model(app, model);
    app_controller_build_memory_model(app, model);
}

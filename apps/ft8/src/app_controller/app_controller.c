#include "app_controller.h"

#include <stdio.h>
#include <string.h>

static bool app_save_config(AppController *app)
{
    char text[2048];
    if (!config_service_serialize(&app->config, text, sizeof(text))) return false;
    return storage_service_write_text_atomic(&app->storage, app->station_path, text);
}

bool app_controller_init(AppController *app, const mini_fs_api_t *fs,
                         const char *data_directory, const char *station_path)
{
    if (app == NULL || fs == NULL || data_directory == NULL || station_path == NULL) {
        return false;
    }
    memset(app, 0, sizeof(*app));

    if (!storage_service_init(&app->storage, fs)) return false;
    if (!storage_service_ensure_directory(&app->storage, data_directory)) return false;

    int path_length = snprintf(app->station_path, sizeof(app->station_path), "%s", station_path);
    if (path_length < 0 || (size_t)path_length >= sizeof(app->station_path)) return false;

    config_service_defaults(&app->config);
    qso_scheduler_init(&app->scheduler);

    char text[2048];
    bool loaded = storage_service_read_text(&app->storage, app->station_path,
                                            text, sizeof(text));
    if (loaded && !config_service_parse(&app->config, text)) return false;

    qso_scheduler_set_skip_tx1(&app->scheduler, app->config.skip_tx1);
    qso_scheduler_set_max_retry(&app->scheduler, app->config.max_retry);

    if (!loaded && !app_save_config(app)) return false;
    return true;
}

void app_controller_build_ui_model(const AppController *app, UiModel *model)
{
    memset(model, 0, sizeof(*model));

    model->profile_index = app->config.profile_index;
    model->profile_count = config_service_profile_count();
    snprintf(model->profile_name, sizeof(model->profile_name), "%s",
             config_service_profile_name(model->profile_index));

    model->band_index = app->config.band_index;
    model->band_count = config_service_band_count(model->profile_index);
    snprintf(model->band_name, sizeof(model->band_name), "%s",
             config_service_band_name(model->profile_index, model->band_index));

    model->skip_tx1 = qso_scheduler_get_skip_tx1(&app->scheduler);
    model->max_retry = qso_scheduler_get_max_retry(&app->scheduler);

    static const char *rx_demo[] = {
        "CQ K1ABC FN42 -10",
        "W6XYZ AG6AQ -08",
        "CQ JA1AAA PM95 -14"
    };
    model->rx_count = sizeof(rx_demo) / sizeof(rx_demo[0]);
    for (size_t i = 0; i < model->rx_count; ++i) {
        snprintf(model->rx_lines[i], UI_TEXT_CAP, "%s", rx_demo[i]);
    }

    model->tx_count = 1u;
    snprintf(model->tx_lines[0], UI_TEXT_CAP, "%s", "TX queue empty (prototype)");
}

bool app_controller_apply_action(AppController *app, const AppAction *action)
{
    if (app == NULL || action == NULL) return false;
    bool changed = false;

    switch (action->type) {
        case APP_ACTION_SET_PROFILE:
            config_service_set_profile(&app->config, action->value.index);
            changed = true;
            break;

        case APP_ACTION_SET_BAND:
            config_service_set_band(&app->config, action->value.index);
            changed = true;
            break;

        case APP_ACTION_SET_SKIP_TX1:
            qso_scheduler_set_skip_tx1(&app->scheduler, action->value.bool_value);
            config_service_set_skip_tx1(&app->config, action->value.bool_value);
            changed = true;
            break;

        case APP_ACTION_SET_MAX_RETRY:
            qso_scheduler_set_max_retry(&app->scheduler, action->value.int_value);
            config_service_set_max_retry(&app->config,
                                         qso_scheduler_get_max_retry(&app->scheduler));
            changed = true;
            break;

        case APP_ACTION_NONE:
        default:
            break;
    }

    if (!changed) return false;
    return app_save_config(app);
}

#ifndef MINIFT8_APP_CONTROLLER_H
#define MINIFT8_APP_CONTROLLER_H

#include <stdbool.h>

#include "minift8/app_types.h"
#include "config_service.h"
#include "qso_scheduler.h"
#include "storage_service.h"

typedef struct {
    Mode active_mode;
    Mode requested_mode;
    bool mode_change_pending;
    ConfigService config;
    QsoScheduler scheduler;
    StorageService storage;
    char station_path[256];
} AppController;

bool app_controller_init(AppController *app, const mini_fs_api_t *fs,
                         const char *data_directory, const char *station_path);
void app_controller_build_ui_model(const AppController *app, UiModel *model);
bool app_controller_apply_action(AppController *app, const AppAction *action);

#endif

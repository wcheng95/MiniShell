#ifndef FT8_APP_CONTROLLER_H
#define FT8_APP_CONTROLLER_H

#include <stdbool.h>
#include <stdint.h>

#include "minishell/api.h"
#include "ft8/app_types.h"

typedef struct AppController AppController;

typedef struct {
    const char *endpoint;
    bool has_explicit_timing;
    int64_t slot_id;
    uint32_t sample_offset;
} AppRxStartConfig;

/* Public controller lifetime. Concrete controller state is private. */
AppController *app_controller_create(const mini_api_t *api,
                                     const char *data_directory,
                                     const char *station_path);
void app_controller_destroy(AppController *app);

bool app_controller_start_rx(AppController *app, const AppRxStartConfig *config);
bool app_controller_step_rx(AppController *app, bool *out_model_changed);
bool app_controller_rx_active(const AppController *app);

/* Build one complete application snapshot; presentation decides what is visible. */
void app_controller_build_model(const AppController *app, UiModel *model);
bool app_controller_apply_action(AppController *app, const AppAction *action);

/* Controller implementation files opt in to the concrete private state. */
#ifdef FT8_APP_CONTROLLER_INTERNAL
#include "app_controller_internal.h"
#endif

#endif

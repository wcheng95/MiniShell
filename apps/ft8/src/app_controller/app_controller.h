#ifndef FT8_APP_CONTROLLER_H
#define FT8_APP_CONTROLLER_H

#include <stdbool.h>
#include <stdint.h>

#include "minishell/api.h"
#include "ft8/app_types.h"
#include "auto_seq.h"
#include "auto_seq_tx_intent.h"
#include "config_service.h"
#include "storage_service.h"
#include "tx_lifecycle.h"

typedef struct AppRxState AppRxState;

typedef struct {
    const char *endpoint;
    bool has_explicit_timing;
    int64_t slot_id;
    uint32_t sample_offset;
} AppRxStartConfig;

typedef struct {
    TxLifecycle lifecycle;
    AutoSeqTxIntent last_intent;
    AutoSeqLogEvent last_log_event;
    int64_t last_tx_slot_id;
    uint64_t simulated_tx_count;
    uint8_t last_intent_valid;
    uint8_t last_log_event_valid;
} AppTxState;

typedef struct {
    const mini_api_t *api;
    ConfigService config;
    AutoSeq auto_seq;
    StorageService storage;
    char station_path[256];
    AppRxState *rx;
    AppTxState tx;
} AppController;

bool app_controller_init(AppController *app, const mini_api_t *api,
                         const char *data_directory, const char *station_path);
bool app_controller_start_rx(AppController *app, const AppRxStartConfig *config);
bool app_controller_step_rx(AppController *app, bool *out_model_changed);
bool app_controller_rx_active(const AppController *app);
void app_controller_shutdown(AppController *app);

void app_controller_build_ui_model(const AppController *app, UiModel *model);
void app_controller_build_memory_model(const AppController *app, UiModel *model);
bool app_controller_apply_action(AppController *app, const AppAction *action);

#endif

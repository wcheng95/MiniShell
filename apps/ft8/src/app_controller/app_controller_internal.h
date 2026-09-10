#ifndef FT8_APP_CONTROLLER_INTERNAL_H
#define FT8_APP_CONTROLLER_INTERNAL_H

#include "app_controller.h"
#include "auto_seq.h"
#include "auto_seq_tx_intent.h"
#include "config_service.h"
#include "storage_service.h"
#include "tx_lifecycle.h"

typedef struct AppRxState AppRxState;

typedef struct {
    TxLifecycle lifecycle;
    AutoSeqTxIntent last_intent;
    AutoSeqLogEvent last_log_event;
    int64_t last_tx_slot_id;
    uint64_t simulated_tx_count;
    uint8_t last_intent_valid;
    uint8_t last_log_event_valid;
} AppTxState;

struct AppController {
    const mini_api_t *api;
    ConfigService config;
    AutoSeq auto_seq;
    StorageService storage;
    char station_path[256];
    AppRxState *rx;
    AppTxState tx;
};

/* Internal lifecycle used by the public create/destroy wrapper and white-box tests. */
bool app_controller_init(AppController *app, const mini_api_t *api,
                         const char *data_directory, const char *station_path);
void app_controller_shutdown(AppController *app);

#endif

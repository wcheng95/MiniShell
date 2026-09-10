#ifndef FT8_APP_CONTROLLER_TX_H
#define FT8_APP_CONTROLLER_TX_H

#include <stdbool.h>
#include <stdint.h>

#include "app_controller.h"

/* Production UTC-driven observation. Missing UTC is a non-fatal no-TX state. */
bool app_controller_step_tx(AppController *app, bool *out_model_changed);

/*
 * Deterministic event boundary used by production after UTC projection and by
 * unit tests without sleeping. This does not key Audio/CAT/RF; completion is
 * simulated synchronously in AS-7.
 */
bool app_controller_observe_tx_slot(AppController *app,
                                    int64_t slot_id,
                                    uint16_t ms_into_slot,
                                    int64_t now_ms,
                                    bool *out_model_changed);

bool app_controller_set_beacon_mode(AppController *app, TxBeaconMode mode);
TxBeaconMode app_controller_get_beacon_mode(const AppController *app);

#endif

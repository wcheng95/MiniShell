#include "tx_lifecycle.h"

#include <string.h>

static bool beacon_mode_valid(TxBeaconMode mode)
{
    return mode == TX_BEACON_OFF || mode == TX_BEACON_EVEN || mode == TX_BEACON_ODD;
}

void tx_lifecycle_init(TxLifecycle *lifecycle)
{
    if (lifecycle == NULL) return;
    memset(lifecycle, 0, sizeof(*lifecycle));
    lifecycle->beacon_mode = TX_BEACON_OFF;
}

bool tx_lifecycle_set_beacon_mode(TxLifecycle *lifecycle, TxBeaconMode mode)
{
    if (lifecycle == NULL || !beacon_mode_valid(mode)) return false;
    lifecycle->beacon_mode = mode;
    return true;
}

TxBeaconMode tx_lifecycle_get_beacon_mode(const TxLifecycle *lifecycle)
{
    return lifecycle != NULL ? lifecycle->beacon_mode : TX_BEACON_OFF;
}

bool tx_lifecycle_beacon_matches(const TxLifecycle *lifecycle, uint8_t slot_parity)
{
    if (lifecycle == NULL) return false;
    slot_parity &= 1u;
    return (lifecycle->beacon_mode == TX_BEACON_EVEN && slot_parity == 0u) ||
           (lifecycle->beacon_mode == TX_BEACON_ODD && slot_parity == 1u);
}

bool tx_lifecycle_observe(TxLifecycle *lifecycle,
                          int64_t slot_id,
                          uint16_t ms_into_slot,
                          TxSlotBoundary *out_boundary,
                          bool *out_has_boundary)
{
    int64_t previous;

    if (lifecycle == NULL || out_boundary == NULL || out_has_boundary == NULL ||
        ms_into_slot >= 15000u) {
        return false;
    }

    memset(out_boundary, 0, sizeof(*out_boundary));
    *out_has_boundary = false;

    if (lifecycle->have_observed_slot == 0u) {
        lifecycle->last_observed_slot_id = slot_id;
        lifecycle->have_observed_slot = 1u;
        return true;
    }

    previous = lifecycle->last_observed_slot_id;
    if (slot_id == previous) return true;

    lifecycle->last_observed_slot_id = slot_id;

    /*
     * Only a naturally adjacent forward slot observed near its beginning is
     * executable. Re-anchors caused by suspend or UTC correction never catch up.
     */
    if (slot_id != previous + 1 || ms_into_slot >= TX_LIFECYCLE_BOUNDARY_WINDOW_MS)
        return true;

    out_boundary->slot_id = slot_id;
    out_boundary->ms_into_slot = ms_into_slot;
    out_boundary->parity = (uint8_t)((uint64_t)slot_id & 1u);
    *out_has_boundary = true;
    return true;
}

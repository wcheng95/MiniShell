#ifndef FT8_TX_LIFECYCLE_H
#define FT8_TX_LIFECYCLE_H

#include <stdbool.h>
#include <stdint.h>

#define TX_LIFECYCLE_BOUNDARY_WINDOW_MS 1000u

typedef uint8_t TxBeaconMode;
#define TX_BEACON_OFF  ((TxBeaconMode)0u)
#define TX_BEACON_EVEN ((TxBeaconMode)1u)
#define TX_BEACON_ODD  ((TxBeaconMode)2u)

typedef struct {
    int64_t slot_id;
    uint16_t ms_into_slot;
    uint8_t parity;
} TxSlotBoundary;

typedef struct {
    int64_t last_observed_slot_id;
    TxBeaconMode beacon_mode;
    uint8_t have_observed_slot;
} TxLifecycle;

void tx_lifecycle_init(TxLifecycle *lifecycle);
bool tx_lifecycle_set_beacon_mode(TxLifecycle *lifecycle, TxBeaconMode mode);
TxBeaconMode tx_lifecycle_get_beacon_mode(const TxLifecycle *lifecycle);
bool tx_lifecycle_beacon_matches(const TxLifecycle *lifecycle, uint8_t slot_parity);

/*
 * Observe UTC-derived slot position.
 *
 * The first observation only anchors the lifecycle. A boundary is emitted
 * only when the immediately following slot is observed within the first
 * TX_LIFECYCLE_BOUNDARY_WINDOW_MS. Missed slots and UTC corrections re-anchor
 * without creating a catch-up transmission opportunity.
 */
bool tx_lifecycle_observe(TxLifecycle *lifecycle,
                          int64_t slot_id,
                          uint16_t ms_into_slot,
                          TxSlotBoundary *out_boundary,
                          bool *out_has_boundary);

#endif

#ifndef FT8_TX_OFFSET_H
#define FT8_TX_OFFSET_H
#include <stdbool.h>
#include <stdint.h>
#include "config_service.h"
#include "auto_seq_tx_intent.h"

/* Pure RF-placement policy. Time is observed and supplied by the controller. */
uint32_t tx_offset_seed(uint64_t monotonic_us, int64_t utc_seconds, uint32_t utc_nanoseconds);
uint32_t tx_offset_next(uint32_t *state);
int16_t tx_offset_random_hz(uint32_t random_value);
bool tx_offset_resolve(Ft8OffsetSource source, int16_t fixed_hz,
                       const AutoSeqTxIntent *intent, uint32_t *state, int16_t *out_hz);
#endif

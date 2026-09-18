#include "tx_offset.h"

#define SEED_MIX UINT32_C(0x9e3779b9)

uint32_t tx_offset_seed(uint64_t monotonic_us, int64_t utc_seconds, uint32_t utc_nanoseconds)
{
    uint64_t seconds = (uint64_t)utc_seconds;
    uint32_t seed = SEED_MIX ^ (uint32_t)monotonic_us ^ (uint32_t)(monotonic_us >> 32) ^
                    (uint32_t)seconds ^ (uint32_t)(seconds >> 32) ^ utc_nanoseconds;
    return seed ? seed : SEED_MIX;
}

uint32_t tx_offset_next(uint32_t *state)
{
    /* xorshift32 (13,17,5), with zero repair to preserve its nonzero invariant. */
    uint32_t x = *state ? *state : SEED_MIX;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

int16_t tx_offset_random_hz(uint32_t random_value)
{
    return (int16_t)(500u + random_value % 2001u);
}

bool tx_offset_resolve(Ft8OffsetSource source, int16_t fixed_hz,
                       const AutoSeqTxIntent *intent, uint32_t *state, int16_t *out_hz)
{
    if (!intent || !state || !out_hz ||
        (intent->type != AUTO_SEQ_TX_INTENT_CQ && intent->type != AUTO_SEQ_TX_INTENT_QSO &&
         intent->type != AUTO_SEQ_TX_INTENT_FREETEXT)) return false;
    switch (source) {
    case FT8_OFFSET_FIXED:
        if (fixed_hz < 300 || fixed_hz > 2700) return false;
        *out_hz = fixed_hz;
        return true;
    case FT8_OFFSET_RX:
        /* Only QSO intents carry an RX fact; free text's default is not one. */
        if (intent->type == AUTO_SEQ_TX_INTENT_QSO && intent->offset_hz >= 300 && intent->offset_hz <= 2700) {
            *out_hz = intent->offset_hz;
            return true;
        }
        break;
    case FT8_OFFSET_RANDOM:
        break;
    default:
        return false;
    }
    *out_hz = tx_offset_random_hz(tx_offset_next(state));
    return true;
}

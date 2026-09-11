#pragma once

#include <stdint.h>

#include "keyer_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    KEYER_KEY_IN_PADDLE = 0,
    KEYER_KEY_IN_PADDLE_R,
    KEYER_KEY_IN_SK_T,
    KEYER_KEY_IN_SK_R,
} keyer_key_in_mode_t;

typedef enum {
    KEYER_KEY_OUT_PADDLE = 0,
    KEYER_KEY_OUT_PADDLE_R,
    KEYER_KEY_OUT_SK,
    KEYER_KEY_OUT_SK_M,
    KEYER_KEY_OUT_OFF,
} keyer_key_out_mode_t;

typedef struct {
    uint8_t wpm;
    keyer_engine_paddle_mode_t paddle_mode;
    keyer_key_in_mode_t key_in_mode;
    keyer_key_out_mode_t key_out_mode;
    uint32_t key_in_tip_line;
    uint32_t key_in_ring_line;
    uint32_t key_out_tip_line;
    uint32_t key_out_ring_line;
} keyer_config_t;

#ifdef __cplusplus
}
#endif

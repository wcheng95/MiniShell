#pragma once

#include <stdbool.h>

#include "keyer_types.h"
#include "minishell/api.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const mini_digital_io_api_t *digital_io;
    mini_digital_io_t tip;
    mini_digital_io_t ring;
    keyer_key_in_mode_t mode;
} keyin_t;

typedef struct {
    bool dit_pressed;
    bool dah_pressed;
    bool straight_pressed;
} keyin_sample_t;

mini_result_t keyin_open(keyin_t *keyin,
                         const mini_digital_io_api_t *digital_io,
                         const keyer_config_t *config);
mini_result_t keyin_read(keyin_t *keyin, keyin_sample_t *out_sample);
void keyin_close(keyin_t *keyin);
keyer_engine_input_mode_t keyin_engine_mode(keyer_key_in_mode_t mode);

#ifdef __cplusplus
}
#endif

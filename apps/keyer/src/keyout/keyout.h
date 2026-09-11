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
    keyer_key_out_mode_t mode;
    uint32_t tip_level;
    uint32_t ring_level;
} keyout_t;

mini_result_t keyout_open(keyout_t *keyout,
                          const mini_digital_io_api_t *digital_io,
                          const keyer_config_t *config);
mini_result_t keyout_apply(keyout_t *keyout,
                           bool key_down,
                           keyer_engine_element_t element,
                           keyer_engine_input_mode_t input_mode);
mini_result_t keyout_release(keyout_t *keyout);
void keyout_close(keyout_t *keyout);

#ifdef __cplusplus
}
#endif

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "minishell/api.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KEYER_SIDETONE_SAMPLE_RATE_HZ 48000u
#define KEYER_SIDETONE_BLOCK_FRAMES   48u

typedef struct {
    const mini_audio_tx_api_t *tx;
    mini_audio_stream_t stream;
    uint32_t phase_q16;
    uint32_t phase_step_q16;
    uint16_t gain_q8;
    bool streaming;
} sidetone_t;

mini_result_t sidetone_open(sidetone_t *sidetone,
                            const mini_audio_api_t *audio,
                            bool enabled,
                            uint32_t pitch_hz);
mini_result_t sidetone_apply(sidetone_t *sidetone, bool key_down);
bool sidetone_streaming(const sidetone_t *sidetone);
void sidetone_close(sidetone_t *sidetone);

#ifdef __cplusplus
}
#endif

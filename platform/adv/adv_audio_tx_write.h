#pragma once

#include <stddef.h>
#include "minishell/api.h"

/* Private fixed-format transport seam: callback returns native success (0) or
 * an error. Codec control/lifecycle remains in the speaker provider. */
typedef int (*adv_audio_tx_transport_fn)(void *handle, const void *frames,
                                        size_t bytes, size_t *written,
                                        uint32_t timeout_ms);

static inline mini_result_t adv_audio_tx_write(adv_audio_tx_transport_fn write_fn,
                                                void *handle, const void *frames,
                                                uint32_t frame_count,
                                                uint32_t *out_frames,
                                                uint32_t timeout_ms,
                                                int native_timeout)
{
    *out_frames = 0u;
    size_t requested = (size_t)frame_count * sizeof(int16_t);
    size_t written = 0u;
    int result = write_fn(handle, frames, requested, &written, timeout_ms);
    /* Odd progress is impossible for S16 frames; never expose a partial frame. */
    if (written > requested || written % sizeof(int16_t) != 0u) return MINI_ERR_IO;
    if (written != 0u) {
        *out_frames = (uint32_t)(written / sizeof(int16_t));
        return MINI_OK;
    }
    return result == native_timeout ? MINI_ERR_TIMEOUT : MINI_ERR_IO;
}

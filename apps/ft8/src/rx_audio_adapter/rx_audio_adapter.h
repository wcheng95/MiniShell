#ifndef RX_AUDIO_ADAPTER_H
#define RX_AUDIO_ADAPTER_H

#include <stddef.h>
#include <stdint.h>

#include "minishell/api.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RX_AUDIO_ADAPTER_SAMPLE_RATE_HZ 12000u
#define RX_AUDIO_ADAPTER_CHANNELS 2u
#define RX_AUDIO_ADAPTER_SAMPLE_FORMAT MINI_AUDIO_SAMPLE_S16

typedef enum {
    RX_AUDIO_ADAPTER_OK = 0,
    RX_AUDIO_ADAPTER_END_OF_STREAM = 1,
    RX_AUDIO_ADAPTER_ERR_INVALID = -1,
    RX_AUDIO_ADAPTER_ERR_UNAVAILABLE = -2,
    RX_AUDIO_ADAPTER_ERR_STATE = -3,
    RX_AUDIO_ADAPTER_ERR_MINISHELL = -4
} RxAudioAdapterStatus;

typedef struct {
    const mini_audio_api_t *audio;
    mini_audio_stream_t stream;
    mini_result_t last_result;
    int initialized;
    int open;
    int started;
} RxAudioAdapter;

/*
 * Bind one MiniFT8 RX adapter instance to the MiniShell Audio service.
 * The adapter owns only MiniFT8's stream handle/lifecycle. MiniShell owns the
 * physical/provider audio resource underneath that handle.
 */
RxAudioAdapterStatus rx_audio_adapter_init(RxAudioAdapter *adapter,
                                           const mini_audio_api_t *audio);

/*
 * Open the fixed MiniFT8 V1 RX transport: 12 kHz, signed 16-bit PCM, 2 channels.
 * Endpoint meaning belongs to the MiniShell provider. A Linux WAV backend uses
 * a logical file path; future live providers may define another endpoint form.
 */
RxAudioAdapterStatus rx_audio_adapter_open(RxAudioAdapter *adapter,
                                           const char *endpoint);
RxAudioAdapterStatus rx_audio_adapter_start(RxAudioAdapter *adapter);

/*
 * Read interleaved S16 frames into caller-owned bounded storage. A successful
 * zero-frame read is allowed for live/timeout-oriented providers. End-of-stream
 * is reported distinctly and does not implicitly close the stream.
 */
RxAudioAdapterStatus rx_audio_adapter_read(RxAudioAdapter *adapter,
                                           int16_t *interleaved_s16,
                                           size_t frame_capacity,
                                           size_t *out_frames,
                                           uint32_t timeout_ms);

RxAudioAdapterStatus rx_audio_adapter_stop(RxAudioAdapter *adapter);

/* Close is cleanup-safe: if the stream is still started, it attempts stop first. */
RxAudioAdapterStatus rx_audio_adapter_close(RxAudioAdapter *adapter);

/* Last raw MiniShell result is diagnostic only; normal control uses adapter status. */
mini_result_t rx_audio_adapter_last_result(const RxAudioAdapter *adapter);

#ifdef __cplusplus
}
#endif

#endif

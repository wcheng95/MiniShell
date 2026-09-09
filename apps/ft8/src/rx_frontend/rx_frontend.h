#ifndef RX_FRONTEND_H
#define RX_FRONTEND_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RX_FRONTEND_INPUT_SAMPLE_RATE_HZ 12000u
#define RX_FRONTEND_OUTPUT_SAMPLE_RATE_HZ 6000u
#define RX_FRONTEND_INPUT_CHANNELS 2u

typedef enum {
    RX_FRONTEND_OK = 0,
    RX_FRONTEND_ERR_INVALID = -1,
    RX_FRONTEND_ERR_NOT_INITIALIZED = -2,
    RX_FRONTEND_ERR_OUTPUT_FULL = -3
} RxFrontendStatus;

typedef enum {
    /* V2-compatible ordinary-audio policy: normalize L/R and average them. */
    RX_FRONTEND_AUDIO_AVERAGE = 0,
    RX_FRONTEND_AUDIO_CHANNEL_0 = 1,
    RX_FRONTEND_AUDIO_CHANNEL_1 = 2
} RxFrontendAudioMode;

typedef struct {
    RxFrontendAudioMode audio_mode;
} RxFrontendConfig;

typedef struct {
    int initialized;
    RxFrontendConfig config;

    /* 0 means the next 12 kHz frame is emitted; 1 means it is skipped. */
    uint8_t decimation_phase;
} RxFrontend;

RxFrontendConfig rx_frontend_baseline_config(void);
RxFrontendStatus rx_frontend_init(RxFrontend *frontend,
                                  const RxFrontendConfig *config);
void rx_frontend_reset_stream(RxFrontend *frontend);
void rx_frontend_destroy(RxFrontend *frontend);

/*
 * Convert interleaved 12 kHz S16 stereo frames into continuous 6 kHz mono
 * float samples. Transport-block boundaries are invisible to the output;
 * decimation phase is retained in RxFrontend.
 *
 * On RX_FRONTEND_ERR_OUTPUT_FULL no input is consumed and state is unchanged.
 */
RxFrontendStatus rx_frontend_process(RxFrontend *frontend,
                                     const int16_t *interleaved_s16,
                                     size_t frame_count,
                                     float *out_samples,
                                     size_t out_capacity,
                                     size_t *out_count);

#ifdef __cplusplus
}
#endif

#endif

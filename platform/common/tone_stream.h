#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "minishell/api.h"
#ifdef __cplusplus
extern "C" {
#endif
#define TONE_STREAM_FRAMES 240U
#define TONE_STREAM_PRIME_CHUNKS 8U
typedef struct {
    bool (*lock)(uint32_t timeout_ms);
    void (*unlock)(void);
    void (*busy_enter)(void);
    void (*busy_exit)(void);
} tone_stream_port_t;
typedef struct { uint32_t samples, generation; } tone_stream_commit_t;
/* One exclusive speaker owner. render/committed are worker-only; commands
 * serialize through the injected mutex and accounting critical section. */
void tone_stream_init(const tone_stream_port_t *port, uint16_t hz);
mini_result_t tone_stream_pitch(uint16_t hz);
mini_result_t tone_stream_enqueue(uint32_t milliseconds);
mini_result_t tone_stream_hold(bool active);
mini_result_t tone_stream_stop(void);
bool tone_stream_busy(void);
void tone_stream_render(int16_t pcm[TONE_STREAM_FRAMES], tone_stream_commit_t *commit);
void tone_stream_committed(const tone_stream_commit_t *commit);
#ifdef __cplusplus
}
#endif

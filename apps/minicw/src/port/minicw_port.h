#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef enum {
    MINI_CW_SCREEN_COLOR_DEFAULT = 0,
    MINI_CW_SCREEN_COLOR_WHITE,
    MINI_CW_SCREEN_COLOR_GREEN,
    MINI_CW_SCREEN_COLOR_CYAN,
} mini_cw_screen_color_t;

/* Pinned Mini-CW sdkconfig uses 100 Hz. Keep its rounding and wrap arithmetic. */
static inline uint32_t minicw_ticks_from_ms(uint32_t ms) { return ms / 10U; }
uint32_t minicw_port_now_ms(void);
uint32_t minicw_port_ticks(void);
bool minicw_port_inputs_ready(void);
bool minicw_port_outputs_ready(void);
uint32_t minicw_port_read(uint32_t line);
void minicw_port_write(uint32_t line, uint32_t level);
void minicw_port_present(const char rows[7][21], const uint8_t colors[7][20]);

bool minicw_port_tone_open(uint16_t hz, uint8_t volume);
void minicw_port_tone_configure(uint16_t hz, uint8_t volume);
void minicw_port_tone_enqueue(uint32_t ms);
void minicw_port_tone_hold(bool active);
void minicw_port_tone_stop(void);
bool minicw_port_tone_busy(void);

/* Recoverable storage errors are reported by the app, never latched as fatal. */
typedef enum { MINICW_FILE_OK, MINICW_FILE_MISSING, MINICW_FILE_INVALID, MINICW_FILE_ERROR } minicw_file_result_t;
minicw_file_result_t minicw_port_file_read(const char *path, char *out, uint32_t capacity);
bool minicw_port_file_replace(const char *directory, const char *temporary, const char *destination,
                              const char *text, uint32_t size);

/* Optional UTC display only; errors never become application failures. */
bool minicw_port_utc_hm(uint8_t *hour, uint8_t *minute);

/* Opaque, single-owner read stream; the public Filesystem handle stays private. */
typedef struct minicw_read_stream *minicw_read_stream_t;
minicw_file_result_t minicw_port_read_open(const char *path, minicw_read_stream_t *out);
bool minicw_port_read_next(minicw_read_stream_t stream, void *buffer, uint32_t size, uint32_t *read);
bool minicw_port_read_close(minicw_read_stream_t stream);

/* Optional Memory service; failures leave the original allocation owned. */
bool minicw_port_memory_resize(void **pointer, uint32_t bytes);
void minicw_port_memory_release(void *pointer);

/* Captured UTC calendar date YYYYMMDD and minute within that date. */
bool minicw_port_utc_minute(uint32_t *date, uint16_t *minute);
bool minicw_port_file_append(const char *directory, const char *path, const char *text, uint32_t size);

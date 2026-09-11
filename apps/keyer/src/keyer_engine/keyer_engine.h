#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KEYER_ENGINE_MIN_WPM 5u
#define KEYER_ENGINE_MAX_WPM 60u
#define KEYER_ENGINE_EVENT_CAPACITY 16u
#define KEYER_ENGINE_DECODER_MAX_ELEMENTS 12u

typedef enum {
    KEYER_ENGINE_ELEMENT_NONE = 0,
    KEYER_ENGINE_ELEMENT_DIT,
    KEYER_ENGINE_ELEMENT_DAH,
} keyer_engine_element_t;

typedef enum {
    KEYER_ENGINE_INPUT_PADDLE = 0,
    KEYER_ENGINE_INPUT_STRAIGHT,
} keyer_engine_input_mode_t;

typedef enum {
    KEYER_ENGINE_PADDLE_IAMBIC_A = 0,
    KEYER_ENGINE_PADDLE_IAMBIC_B,
    KEYER_ENGINE_PADDLE_BUG,
} keyer_engine_paddle_mode_t;

typedef enum {
    KEYER_ENGINE_EVENT_NONE = 0,
    KEYER_ENGINE_EVENT_DIT,
    KEYER_ENGINE_EVENT_DAH,
    KEYER_ENGINE_EVENT_CHAR,
    KEYER_ENGINE_EVENT_BACKSPACE,
    KEYER_ENGINE_EVENT_ENTER,
    KEYER_ENGINE_EVENT_WORD_SPACE,
} keyer_engine_event_type_t;

typedef struct {
    keyer_engine_event_type_t type;
    keyer_engine_element_t element;
    char ch;
    uint32_t duration_us;
} keyer_engine_event_t;

typedef struct {
    uint8_t wpm;
    keyer_engine_input_mode_t input_mode;
    keyer_engine_paddle_mode_t paddle_mode;
} keyer_engine_config_t;

typedef enum {
    KEYER_ENGINE_PHASE_IDLE = 0,
    KEYER_ENGINE_PHASE_WAIT_GAP,
} keyer_engine_phase_t;

typedef struct {
    char pattern[KEYER_ENGINE_DECODER_MAX_ELEMENTS + 1u];
    uint8_t len;
    bool overflow;
} keyer_engine_decoder_t;

typedef struct {
    keyer_engine_config_t config;

    bool key_down;
    keyer_engine_phase_t phase;
    keyer_engine_element_t last_element;
    uint64_t element_end_us;
    uint64_t ready_us;

    bool dit_memory;
    bool dah_memory;
    bool squeeze_latched;
    bool extra_pending;
    bool bug_dah_down;

    bool straight_down;
    uint64_t straight_start_us;

    keyer_engine_decoder_t decoder;
    uint64_t decoder_last_element_end_us;
    bool decoder_char_finalized;
    bool decoder_space_emitted;

    keyer_engine_event_t events[KEYER_ENGINE_EVENT_CAPACITY];
    uint8_t event_head;
    uint8_t event_tail;
    uint8_t event_count;
} keyer_engine_t;

void keyer_engine_init(keyer_engine_t *engine,
                       const keyer_engine_config_t *config,
                       uint64_t now_us);
void keyer_engine_reset(keyer_engine_t *engine, uint64_t now_us);

void keyer_engine_set_wpm(keyer_engine_t *engine, uint8_t wpm);
uint8_t keyer_engine_get_wpm(const keyer_engine_t *engine);
uint32_t keyer_engine_dit_us(const keyer_engine_t *engine);

void keyer_engine_set_input_mode(keyer_engine_t *engine,
                                 keyer_engine_input_mode_t mode,
                                 uint64_t now_us);
void keyer_engine_set_paddle_mode(keyer_engine_t *engine,
                                  keyer_engine_paddle_mode_t mode,
                                  uint64_t now_us);

/*
 * Advance the pure keyer state machine to now_us.
 *
 * Paddle mode consumes dit_pressed/dah_pressed and ignores straight_pressed.
 * Straight mode consumes straight_pressed and ignores dit_pressed/dah_pressed.
 *
 * Paddle reversal is intentionally outside the engine: KeyIn maps physical
 * lines to logical dit/dah before calling this function.
 */
void keyer_engine_step(keyer_engine_t *engine,
                       uint64_t now_us,
                       bool dit_pressed,
                       bool dah_pressed,
                       bool straight_pressed);

bool keyer_engine_key_down(const keyer_engine_t *engine);
keyer_engine_element_t keyer_engine_last_element(const keyer_engine_t *engine);

bool keyer_engine_poll_event(keyer_engine_t *engine,
                             keyer_engine_event_t *out_event);
void keyer_engine_clear_events(keyer_engine_t *engine);

#ifdef __cplusplus
}
#endif

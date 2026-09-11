#include "keyer_engine.h"

#include <stddef.h>
#include <string.h>

#include "keyer_decoder.h"

static uint8_t clamp_wpm(uint8_t wpm)
{
    if (wpm < KEYER_ENGINE_MIN_WPM) return KEYER_ENGINE_MIN_WPM;
    if (wpm > KEYER_ENGINE_MAX_WPM) return KEYER_ENGINE_MAX_WPM;
    return wpm;
}

static keyer_engine_input_mode_t clamp_input_mode(keyer_engine_input_mode_t mode)
{
    return mode == KEYER_ENGINE_INPUT_STRAIGHT ? mode : KEYER_ENGINE_INPUT_PADDLE;
}

static keyer_engine_paddle_mode_t clamp_paddle_mode(keyer_engine_paddle_mode_t mode)
{
    switch (mode) {
    case KEYER_ENGINE_PADDLE_IAMBIC_A:
    case KEYER_ENGINE_PADDLE_IAMBIC_B:
    case KEYER_ENGINE_PADDLE_BUG:
        return mode;
    default:
        return KEYER_ENGINE_PADDLE_IAMBIC_A;
    }
}

static uint32_t wpm_to_dit_us(uint8_t wpm)
{
    uint32_t clamped = clamp_wpm(wpm);
    uint32_t dit = 1200000u / clamped;
    return dit == 0u ? 1u : dit;
}

static keyer_engine_element_t opposite_element(keyer_engine_element_t element)
{
    switch (element) {
    case KEYER_ENGINE_ELEMENT_DIT:
        return KEYER_ENGINE_ELEMENT_DAH;
    case KEYER_ENGINE_ELEMENT_DAH:
        return KEYER_ENGINE_ELEMENT_DIT;
    case KEYER_ENGINE_ELEMENT_NONE:
    default:
        return KEYER_ENGINE_ELEMENT_DIT;
    }
}

static void clear_auto_state(keyer_engine_t *engine)
{
    engine->phase = KEYER_ENGINE_PHASE_IDLE;
    engine->last_element = KEYER_ENGINE_ELEMENT_NONE;
    engine->element_end_us = 0u;
    engine->ready_us = 0u;
    engine->dit_memory = false;
    engine->dah_memory = false;
    engine->squeeze_latched = false;
    engine->extra_pending = false;
    engine->bug_dah_down = false;
}

void keyer_engine_clear_events(keyer_engine_t *engine)
{
    if (engine == NULL) return;
    engine->event_head = 0u;
    engine->event_tail = 0u;
    engine->event_count = 0u;
}

static void push_event(keyer_engine_t *engine,
                       keyer_engine_event_type_t type,
                       keyer_engine_element_t element,
                       char ch,
                       uint32_t duration_us)
{
    if (engine == NULL || type == KEYER_ENGINE_EVENT_NONE) return;

    if (engine->event_count == KEYER_ENGINE_EVENT_CAPACITY) {
        engine->event_tail = (uint8_t)((engine->event_tail + 1u) % KEYER_ENGINE_EVENT_CAPACITY);
        --engine->event_count;
    }

    keyer_engine_event_t *event = &engine->events[engine->event_head];
    event->type = type;
    event->element = element;
    event->ch = ch;
    event->duration_us = duration_us;

    engine->event_head = (uint8_t)((engine->event_head + 1u) % KEYER_ENGINE_EVENT_CAPACITY);
    ++engine->event_count;
}

static void reset_decoder_state(keyer_engine_t *engine)
{
    keyer_decoder_reset(&engine->decoder);
    engine->decoder_last_element_end_us = 0u;
    engine->decoder_char_finalized = false;
    engine->decoder_space_emitted = false;
}

static void emit_decoder_result(keyer_engine_t *engine, keyer_decoder_result_t result)
{
    switch (result.type) {
    case KEYER_DECODER_RESULT_CHAR:
        push_event(engine, KEYER_ENGINE_EVENT_CHAR, KEYER_ENGINE_ELEMENT_NONE,
                   result.ch, 0u);
        engine->decoder_char_finalized = true;
        engine->decoder_space_emitted = false;
        break;
    case KEYER_DECODER_RESULT_BACKSPACE:
        push_event(engine, KEYER_ENGINE_EVENT_BACKSPACE, KEYER_ENGINE_ELEMENT_NONE,
                   '\b', 0u);
        engine->decoder_char_finalized = false;
        engine->decoder_space_emitted = true;
        break;
    case KEYER_DECODER_RESULT_ENTER:
        push_event(engine, KEYER_ENGINE_EVENT_ENTER, KEYER_ENGINE_ELEMENT_NONE,
                   '\n', 0u);
        engine->decoder_char_finalized = false;
        engine->decoder_space_emitted = true;
        break;
    case KEYER_DECODER_RESULT_SPACE:
        push_event(engine, KEYER_ENGINE_EVENT_WORD_SPACE, KEYER_ENGINE_ELEMENT_NONE,
                   ' ', 0u);
        engine->decoder_char_finalized = false;
        engine->decoder_space_emitted = true;
        break;
    case KEYER_DECODER_RESULT_INVALID:
        /* Mini-CW compatibility: '~' is the visible invalid-Morse marker. */
        push_event(engine, KEYER_ENGINE_EVENT_CHAR, KEYER_ENGINE_ELEMENT_NONE,
                   '~', 0u);
        engine->decoder_char_finalized = true;
        engine->decoder_space_emitted = false;
        break;
    case KEYER_DECODER_RESULT_NONE:
    default:
        break;
    }
}

static void update_decode_gaps(keyer_engine_t *engine, uint64_t now_us)
{
    if (engine->decoder_last_element_end_us == 0u) return;

    uint64_t unit_us = keyer_engine_dit_us(engine);

    if (keyer_decoder_has_pending(&engine->decoder)) {
        uint64_t char_due = engine->decoder_last_element_end_us + (3u * unit_us);
        if (now_us < char_due) return;
        emit_decoder_result(engine, keyer_decoder_finalize(&engine->decoder));
    }

    if (!engine->decoder_char_finalized || engine->decoder_space_emitted) return;

    uint64_t word_due = engine->decoder_last_element_end_us + (7u * unit_us);
    if (now_us >= word_due) {
        push_event(engine, KEYER_ENGINE_EVENT_WORD_SPACE, KEYER_ENGINE_ELEMENT_NONE,
                   ' ', 0u);
        engine->decoder_space_emitted = true;
    }
}

static void start_auto_element(keyer_engine_t *engine,
                               keyer_engine_element_t element,
                               uint64_t now_us)
{
    if (element == KEYER_ENGINE_ELEMENT_NONE) return;

    uint32_t unit_us = keyer_engine_dit_us(engine);
    uint32_t duration_us = element == KEYER_ENGINE_ELEMENT_DAH ? 3u * unit_us : unit_us;

    engine->key_down = true;
    engine->last_element = element;
    engine->phase = KEYER_ENGINE_PHASE_WAIT_GAP;
    engine->element_end_us = now_us + duration_us;
    engine->ready_us = engine->element_end_us + unit_us;

    if (element == KEYER_ENGINE_ELEMENT_DIT) {
        engine->dit_memory = false;
        push_event(engine, KEYER_ENGINE_EVENT_DIT, element, '.', duration_us);
        keyer_decoder_append(&engine->decoder, false);
    } else {
        engine->dah_memory = false;
        push_event(engine, KEYER_ENGINE_EVENT_DAH, element, '-', duration_us);
        keyer_decoder_append(&engine->decoder, true);
    }

    engine->decoder_last_element_end_us = engine->element_end_us;
    engine->decoder_char_finalized = false;
    engine->decoder_space_emitted = false;
}

static void update_iambic_memory(keyer_engine_t *engine,
                                 bool dit_pressed,
                                 bool dah_pressed,
                                 uint64_t now_us)
{
    bool both_pressed = dit_pressed && dah_pressed;

    if (engine->phase != KEYER_ENGINE_PHASE_WAIT_GAP) return;

    if (both_pressed) engine->squeeze_latched = true;

    if (dit_pressed && engine->last_element == KEYER_ENGINE_ELEMENT_DAH) {
        engine->dit_memory = true;
    }
    if (dah_pressed && engine->last_element == KEYER_ENGINE_ELEMENT_DIT) {
        engine->dah_memory = true;
    }

    /*
     * Preserve the current Mini-CW field behavior exactly. Its June 2026
     * hardware fix intentionally applies the squeeze-release extra element to
     * the enum/display selection named Iambic A. Do not silently normalize the
     * labels during this first port; a future semantic change needs its own
     * explicit migration and regression tests.
     */
    if (engine->config.paddle_mode == KEYER_ENGINE_PADDLE_IAMBIC_A &&
        !dit_pressed && !dah_pressed && engine->squeeze_latched &&
        !engine->extra_pending && now_us < engine->element_end_us) {
        engine->extra_pending = true;
    }
}

static keyer_engine_element_t choose_next_iambic(keyer_engine_t *engine,
                                                  bool dit_pressed,
                                                  bool dah_pressed)
{
    if (engine->extra_pending) {
        engine->extra_pending = false;
        engine->squeeze_latched = false;
        return opposite_element(engine->last_element);
    }

    if (dit_pressed && dah_pressed) {
        engine->squeeze_latched = true;
        return opposite_element(engine->last_element);
    }

    if (engine->dit_memory) return KEYER_ENGINE_ELEMENT_DIT;
    if (engine->dah_memory) return KEYER_ENGINE_ELEMENT_DAH;
    if (dit_pressed) return KEYER_ENGINE_ELEMENT_DIT;
    if (dah_pressed) return KEYER_ENGINE_ELEMENT_DAH;
    return KEYER_ENGINE_ELEMENT_NONE;
}

static void update_iambic(keyer_engine_t *engine,
                          uint64_t now_us,
                          bool dit_pressed,
                          bool dah_pressed)
{
    update_iambic_memory(engine, dit_pressed, dah_pressed, now_us);

    if (engine->phase == KEYER_ENGINE_PHASE_WAIT_GAP) {
        if (engine->key_down && now_us >= engine->element_end_us) {
            engine->key_down = false;
        }
        if (now_us < engine->ready_us) return;
        engine->phase = KEYER_ENGINE_PHASE_IDLE;
    }

    update_decode_gaps(engine, now_us);

    keyer_engine_element_t next = choose_next_iambic(engine, dit_pressed, dah_pressed);
    if (next != KEYER_ENGINE_ELEMENT_NONE) {
        start_auto_element(engine, next, now_us);
        return;
    }

    engine->key_down = false;
    clear_auto_state(engine);
}

static void update_bug(keyer_engine_t *engine,
                       uint64_t now_us,
                       bool dit_pressed,
                       bool dah_pressed)
{
    engine->squeeze_latched = false;
    engine->extra_pending = false;

    if (dah_pressed) {
        if (!engine->bug_dah_down) {
            uint32_t duration_us = 3u * keyer_engine_dit_us(engine);
            engine->key_down = true;
            engine->bug_dah_down = true;
            engine->last_element = KEYER_ENGINE_ELEMENT_DAH;
            push_event(engine, KEYER_ENGINE_EVENT_DAH, KEYER_ENGINE_ELEMENT_DAH,
                       '-', duration_us);
            keyer_decoder_append(&engine->decoder, true);
            engine->decoder_char_finalized = false;
            engine->decoder_space_emitted = false;
        }
        engine->decoder_last_element_end_us = now_us;
        return;
    }

    if (engine->bug_dah_down) {
        engine->bug_dah_down = false;
        engine->key_down = false;
        engine->decoder_last_element_end_us = now_us;
    }

    if (engine->phase == KEYER_ENGINE_PHASE_WAIT_GAP) {
        if (engine->key_down && now_us >= engine->element_end_us) {
            engine->key_down = false;
        }
        if (now_us < engine->ready_us) return;
        engine->phase = KEYER_ENGINE_PHASE_IDLE;
    }

    update_decode_gaps(engine, now_us);

    if (dit_pressed) {
        start_auto_element(engine, KEYER_ENGINE_ELEMENT_DIT, now_us);
        return;
    }

    engine->key_down = false;
    engine->phase = KEYER_ENGINE_PHASE_IDLE;
    engine->last_element = KEYER_ENGINE_ELEMENT_NONE;
    engine->dit_memory = false;
    engine->dah_memory = false;
}

static void update_straight(keyer_engine_t *engine,
                            uint64_t now_us,
                            bool pressed)
{
    clear_auto_state(engine);

    if (pressed && !engine->straight_down) {
        engine->straight_down = true;
        engine->straight_start_us = now_us;
        engine->key_down = true;
        return;
    }

    if (!pressed && engine->straight_down) {
        uint64_t elapsed = now_us - engine->straight_start_us;
        uint32_t duration_us = elapsed > UINT32_MAX ? UINT32_MAX : (uint32_t)elapsed;
        uint32_t unit_us = keyer_engine_dit_us(engine);
        keyer_engine_element_t element =
            elapsed <= (uint64_t)unit_us * 2u ? KEYER_ENGINE_ELEMENT_DIT
                                             : KEYER_ENGINE_ELEMENT_DAH;

        engine->straight_down = false;
        engine->straight_start_us = 0u;
        engine->key_down = false;
        engine->last_element = element;

        push_event(engine,
                   element == KEYER_ENGINE_ELEMENT_DAH ? KEYER_ENGINE_EVENT_DAH
                                                       : KEYER_ENGINE_EVENT_DIT,
                   element,
                   element == KEYER_ENGINE_ELEMENT_DAH ? '-' : '.',
                   duration_us);
        keyer_decoder_append(&engine->decoder, element == KEYER_ENGINE_ELEMENT_DAH);
        engine->decoder_last_element_end_us = now_us;
        engine->decoder_char_finalized = false;
        engine->decoder_space_emitted = false;
    }

    if (!pressed) update_decode_gaps(engine, now_us);
}

void keyer_engine_init(keyer_engine_t *engine,
                       const keyer_engine_config_t *config,
                       uint64_t now_us)
{
    if (engine == NULL) return;

    memset(engine, 0, sizeof(*engine));
    engine->config.wpm = 20u;
    engine->config.input_mode = KEYER_ENGINE_INPUT_PADDLE;
    engine->config.paddle_mode = KEYER_ENGINE_PADDLE_IAMBIC_A;

    if (config != NULL) {
        engine->config.wpm = clamp_wpm(config->wpm);
        engine->config.input_mode = clamp_input_mode(config->input_mode);
        engine->config.paddle_mode = clamp_paddle_mode(config->paddle_mode);
    }

    keyer_engine_reset(engine, now_us);
}

void keyer_engine_reset(keyer_engine_t *engine, uint64_t now_us)
{
    (void)now_us;
    if (engine == NULL) return;

    engine->key_down = false;
    engine->straight_down = false;
    engine->straight_start_us = 0u;
    clear_auto_state(engine);
    reset_decoder_state(engine);
    keyer_engine_clear_events(engine);
}

void keyer_engine_set_wpm(keyer_engine_t *engine, uint8_t wpm)
{
    if (engine == NULL) return;
    engine->config.wpm = clamp_wpm(wpm);
}

uint8_t keyer_engine_get_wpm(const keyer_engine_t *engine)
{
    return engine == NULL ? 0u : engine->config.wpm;
}

uint32_t keyer_engine_dit_us(const keyer_engine_t *engine)
{
    if (engine == NULL) return 0u;
    return wpm_to_dit_us(engine->config.wpm);
}

void keyer_engine_set_input_mode(keyer_engine_t *engine,
                                 keyer_engine_input_mode_t mode,
                                 uint64_t now_us)
{
    if (engine == NULL) return;
    mode = clamp_input_mode(mode);
    if (engine->config.input_mode == mode) return;
    engine->config.input_mode = mode;
    keyer_engine_reset(engine, now_us);
}

void keyer_engine_set_paddle_mode(keyer_engine_t *engine,
                                  keyer_engine_paddle_mode_t mode,
                                  uint64_t now_us)
{
    if (engine == NULL) return;
    mode = clamp_paddle_mode(mode);
    if (engine->config.paddle_mode == mode) return;
    engine->config.paddle_mode = mode;
    keyer_engine_reset(engine, now_us);
}

void keyer_engine_step(keyer_engine_t *engine,
                       uint64_t now_us,
                       bool dit_pressed,
                       bool dah_pressed,
                       bool straight_pressed)
{
    if (engine == NULL) return;

    if (engine->config.input_mode == KEYER_ENGINE_INPUT_STRAIGHT) {
        update_straight(engine, now_us, straight_pressed);
        return;
    }

    engine->straight_down = false;
    engine->straight_start_us = 0u;

    if (engine->config.paddle_mode == KEYER_ENGINE_PADDLE_BUG) {
        update_bug(engine, now_us, dit_pressed, dah_pressed);
    } else {
        update_iambic(engine, now_us, dit_pressed, dah_pressed);
    }
}

bool keyer_engine_key_down(const keyer_engine_t *engine)
{
    return engine != NULL && engine->key_down;
}

keyer_engine_element_t keyer_engine_last_element(const keyer_engine_t *engine)
{
    return engine == NULL ? KEYER_ENGINE_ELEMENT_NONE : engine->last_element;
}

bool keyer_engine_poll_event(keyer_engine_t *engine,
                             keyer_engine_event_t *out_event)
{
    if (engine == NULL || out_event == NULL || engine->event_count == 0u) return false;

    *out_event = engine->events[engine->event_tail];
    engine->event_tail = (uint8_t)((engine->event_tail + 1u) % KEYER_ENGINE_EVENT_CAPACITY);
    --engine->event_count;
    return true;
}

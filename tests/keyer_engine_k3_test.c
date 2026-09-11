#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "keyer_engine.h"

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        exit(1); \
    } \
} while (0)

static keyer_engine_t make_engine(uint8_t wpm,
                                  keyer_engine_input_mode_t input_mode,
                                  keyer_engine_paddle_mode_t paddle_mode)
{
    keyer_engine_t engine;
    keyer_engine_config_t config = {
        .wpm = wpm,
        .input_mode = input_mode,
        .paddle_mode = paddle_mode,
    };
    keyer_engine_init(&engine, &config, 0u);
    return engine;
}

static keyer_engine_event_t next_event(keyer_engine_t *engine,
                                       keyer_engine_event_type_t expected)
{
    keyer_engine_event_t event;
    CHECK(keyer_engine_poll_event(engine, &event));
    CHECK(event.type == expected);
    return event;
}

static void expect_no_event(keyer_engine_t *engine)
{
    keyer_engine_event_t event;
    CHECK(!keyer_engine_poll_event(engine, &event));
}

static void test_wpm_and_single_dit(void)
{
    keyer_engine_t engine = make_engine(20u, KEYER_ENGINE_INPUT_PADDLE,
                                       KEYER_ENGINE_PADDLE_IAMBIC_B);
    CHECK(keyer_engine_get_wpm(&engine) == 20u);
    CHECK(keyer_engine_dit_us(&engine) == 60000u);

    keyer_engine_set_wpm(&engine, 1u);
    CHECK(keyer_engine_get_wpm(&engine) == KEYER_ENGINE_MIN_WPM);
    keyer_engine_set_wpm(&engine, 99u);
    CHECK(keyer_engine_get_wpm(&engine) == KEYER_ENGINE_MAX_WPM);
    keyer_engine_set_wpm(&engine, 20u);

    keyer_engine_step(&engine, 0u, true, false, false);
    CHECK(keyer_engine_key_down(&engine));
    keyer_engine_event_t dit = next_event(&engine, KEYER_ENGINE_EVENT_DIT);
    CHECK(dit.element == KEYER_ENGINE_ELEMENT_DIT);
    CHECK(dit.duration_us == 60000u);

    keyer_engine_step(&engine, 59999u, false, false, false);
    CHECK(keyer_engine_key_down(&engine));
    keyer_engine_step(&engine, 60000u, false, false, false);
    CHECK(!keyer_engine_key_down(&engine));
    keyer_engine_step(&engine, 120000u, false, false, false);
    CHECK(!keyer_engine_key_down(&engine));
    expect_no_event(&engine);

    keyer_engine_step(&engine, 239999u, false, false, false);
    expect_no_event(&engine);
    keyer_engine_step(&engine, 240000u, false, false, false);
    keyer_engine_event_t decoded = next_event(&engine, KEYER_ENGINE_EVENT_CHAR);
    CHECK(decoded.ch == 'E');

    keyer_engine_step(&engine, 480000u, false, false, false);
    keyer_engine_event_t space = next_event(&engine, KEYER_ENGINE_EVENT_WORD_SPACE);
    CHECK(space.ch == ' ');
}

static void test_held_dit_repeats(void)
{
    keyer_engine_t engine = make_engine(20u, KEYER_ENGINE_INPUT_PADDLE,
                                       KEYER_ENGINE_PADDLE_IAMBIC_B);

    keyer_engine_step(&engine, 0u, true, false, false);
    (void)next_event(&engine, KEYER_ENGINE_EVENT_DIT);
    keyer_engine_step(&engine, 60000u, true, false, false);
    CHECK(!keyer_engine_key_down(&engine));
    keyer_engine_step(&engine, 120000u, true, false, false);
    CHECK(keyer_engine_key_down(&engine));
    (void)next_event(&engine, KEYER_ENGINE_EVENT_DIT);
}

static void test_squeeze_alternates(void)
{
    keyer_engine_t engine = make_engine(20u, KEYER_ENGINE_INPUT_PADDLE,
                                       KEYER_ENGINE_PADDLE_IAMBIC_B);

    keyer_engine_step(&engine, 0u, true, true, false);
    CHECK(next_event(&engine, KEYER_ENGINE_EVENT_DIT).element == KEYER_ENGINE_ELEMENT_DIT);
    keyer_engine_step(&engine, 60000u, true, true, false);
    keyer_engine_step(&engine, 120000u, true, true, false);
    CHECK(next_event(&engine, KEYER_ENGINE_EVENT_DAH).element == KEYER_ENGINE_ELEMENT_DAH);
    CHECK(keyer_engine_key_down(&engine));
}

static void test_opposite_memory(void)
{
    keyer_engine_t engine = make_engine(20u, KEYER_ENGINE_INPUT_PADDLE,
                                       KEYER_ENGINE_PADDLE_IAMBIC_B);

    keyer_engine_step(&engine, 0u, true, false, false);
    (void)next_event(&engine, KEYER_ENGINE_EVENT_DIT);

    keyer_engine_step(&engine, 30000u, false, true, false);
    keyer_engine_step(&engine, 70000u, false, false, false);
    keyer_engine_step(&engine, 120000u, false, false, false);
    CHECK(next_event(&engine, KEYER_ENGINE_EVENT_DAH).element == KEYER_ENGINE_ELEMENT_DAH);
}

static void test_minicw_iambic_ab_compatibility(void)
{
    keyer_engine_t a = make_engine(20u, KEYER_ENGINE_INPUT_PADDLE,
                                  KEYER_ENGINE_PADDLE_IAMBIC_A);
    keyer_engine_t b = make_engine(20u, KEYER_ENGINE_INPUT_PADDLE,
                                  KEYER_ENGINE_PADDLE_IAMBIC_B);

    /* Mini-CW's field-validated June swap makes enum/display A carry the
     * squeeze-release extra element. Preserve that behavior in the first port. */
    keyer_engine_step(&a, 0u, true, true, false);
    (void)next_event(&a, KEYER_ENGINE_EVENT_DIT);
    keyer_engine_step(&a, 30000u, false, false, false);
    keyer_engine_step(&a, 60000u, false, false, false);
    keyer_engine_step(&a, 120000u, false, false, false);
    CHECK(next_event(&a, KEYER_ENGINE_EVENT_DAH).element == KEYER_ENGINE_ELEMENT_DAH);

    keyer_engine_step(&b, 0u, true, true, false);
    (void)next_event(&b, KEYER_ENGINE_EVENT_DIT);
    keyer_engine_step(&b, 30000u, false, false, false);
    keyer_engine_step(&b, 60000u, false, false, false);
    keyer_engine_step(&b, 120000u, false, false, false);
    expect_no_event(&b);
}

static void test_straight_key(void)
{
    keyer_engine_t engine = make_engine(20u, KEYER_ENGINE_INPUT_STRAIGHT,
                                       KEYER_ENGINE_PADDLE_IAMBIC_A);

    keyer_engine_step(&engine, 1000u, false, false, true);
    CHECK(keyer_engine_key_down(&engine));
    keyer_engine_step(&engine, 51000u, false, false, false);
    CHECK(!keyer_engine_key_down(&engine));
    keyer_engine_event_t dit = next_event(&engine, KEYER_ENGINE_EVENT_DIT);
    CHECK(dit.duration_us == 50000u);

    keyer_engine_step(&engine, 231000u, false, false, false);
    CHECK(next_event(&engine, KEYER_ENGINE_EVENT_CHAR).ch == 'E');

    keyer_engine_reset(&engine, 300000u);
    keyer_engine_step(&engine, 300000u, false, false, true);
    keyer_engine_step(&engine, 440000u, false, false, false);
    keyer_engine_event_t dah = next_event(&engine, KEYER_ENGINE_EVENT_DAH);
    CHECK(dah.duration_us == 140000u);
    keyer_engine_step(&engine, 620000u, false, false, false);
    CHECK(next_event(&engine, KEYER_ENGINE_EVENT_CHAR).ch == 'T');
}

static void test_bug_manual_dah(void)
{
    keyer_engine_t engine = make_engine(20u, KEYER_ENGINE_INPUT_PADDLE,
                                       KEYER_ENGINE_PADDLE_BUG);

    keyer_engine_step(&engine, 0u, false, true, false);
    CHECK(keyer_engine_key_down(&engine));
    CHECK(next_event(&engine, KEYER_ENGINE_EVENT_DAH).element == KEYER_ENGINE_ELEMENT_DAH);

    keyer_engine_step(&engine, 200000u, false, true, false);
    CHECK(keyer_engine_key_down(&engine));
    expect_no_event(&engine);

    keyer_engine_step(&engine, 210000u, false, false, false);
    CHECK(!keyer_engine_key_down(&engine));
}

int main(void)
{
    test_wpm_and_single_dit();
    test_held_dit_repeats();
    test_squeeze_alternates();
    test_opposite_memory();
    test_minicw_iambic_ab_compatibility();
    test_straight_key();
    test_bug_manual_dah();

    puts("keyer_engine_k3_test: PASS");
    return 0;
}

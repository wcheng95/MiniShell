#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config_service.h"
#include "keyin.h"
#include "keyout.h"

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        exit(1); \
    } \
} while (0)

typedef struct {
    bool opened;
    uint32_t mode;
    uint32_t level;
} fake_line_t;

static fake_line_t s_lines[64];

static mini_result_t fake_open(const mini_digital_io_config_t *config,
                               mini_digital_io_t *out_line)
{
    if (config == NULL || out_line == NULL || config->line_id >= 64u ||
        config->initial_level > 1u) return MINI_ERR_INVALID;
    if (s_lines[config->line_id].opened) return MINI_ERR_EXISTS;

    s_lines[config->line_id].opened = true;
    s_lines[config->line_id].mode = config->mode;
    s_lines[config->line_id].level = config->initial_level;
    *out_line = config->line_id + 1u;
    return MINI_OK;
}

static mini_result_t fake_read(mini_digital_io_t line, uint32_t *out_level)
{
    uint32_t id;
    if (line == MINI_DIGITAL_IO_INVALID || out_level == NULL) return MINI_ERR_INVALID;
    id = line - 1u;
    if (id >= 64u || !s_lines[id].opened) return MINI_ERR_BAD_HANDLE;
    *out_level = s_lines[id].level;
    return MINI_OK;
}

static mini_result_t fake_write(mini_digital_io_t line, uint32_t level)
{
    uint32_t id;
    if (line == MINI_DIGITAL_IO_INVALID || level > 1u) return MINI_ERR_INVALID;
    id = line - 1u;
    if (id >= 64u || !s_lines[id].opened) return MINI_ERR_BAD_HANDLE;
    s_lines[id].level = level;
    return MINI_OK;
}

static mini_result_t fake_close(mini_digital_io_t line)
{
    uint32_t id;
    if (line == MINI_DIGITAL_IO_INVALID) return MINI_ERR_INVALID;
    id = line - 1u;
    if (id >= 64u || !s_lines[id].opened) return MINI_ERR_BAD_HANDLE;
    s_lines[id].opened = false;
    return MINI_OK;
}

static const mini_digital_io_api_t DIGITAL_IO = {
    .struct_size = sizeof(mini_digital_io_api_t),
    .capabilities = MINI_DIGITAL_IO_CAP_INPUT |
                    MINI_DIGITAL_IO_CAP_INPUT_PULLUP |
                    MINI_DIGITAL_IO_CAP_OUTPUT |
                    MINI_DIGITAL_IO_CAP_OUTPUT_OPEN_DRAIN,
    .open = fake_open,
    .read = fake_read,
    .write = fake_write,
    .close = fake_close,
};

static void reset_fake(void)
{
    memset(s_lines, 0, sizeof(s_lines));
    for (size_t i = 0u; i < 64u; ++i) s_lines[i].level = 1u;
}

static keyer_config_t default_config(void)
{
    keyer_config_t config;
    config_service_defaults(&config);
    return config;
}

static void test_defaults(void)
{
    keyer_config_t config = default_config();
    CHECK(config.wpm == 20u);
    CHECK(config.key_in_mode == KEYER_KEY_IN_PADDLE);
    CHECK(config.paddle_mode == KEYER_ENGINE_PADDLE_IAMBIC_A);
    CHECK(config.key_out_mode == KEYER_KEY_OUT_SK);
    CHECK(config.key_in_tip_line == 13u);
    CHECK(config.key_in_ring_line == 15u);
    CHECK(config.key_out_tip_line == 3u);
    CHECK(config.key_out_ring_line == 6u);
}

static void test_keyin_modes(void)
{
    keyin_t keyin;
    keyin_sample_t sample;
    keyer_config_t config = default_config();

    reset_fake();
    CHECK(keyin_open(&keyin, &DIGITAL_IO, &config) == MINI_OK);
    CHECK(s_lines[13].mode == MINI_DIGITAL_IO_MODE_INPUT_PULLUP);
    CHECK(s_lines[15].mode == MINI_DIGITAL_IO_MODE_INPUT_PULLUP);

    s_lines[13].level = 0u;
    s_lines[15].level = 1u;
    CHECK(keyin_read(&keyin, &sample) == MINI_OK);
    CHECK(sample.dit_pressed && !sample.dah_pressed && !sample.straight_pressed);
    keyin_close(&keyin);

    config.key_in_mode = KEYER_KEY_IN_PADDLE_R;
    CHECK(keyin_open(&keyin, &DIGITAL_IO, &config) == MINI_OK);
    s_lines[13].level = 0u;
    s_lines[15].level = 1u;
    CHECK(keyin_read(&keyin, &sample) == MINI_OK);
    CHECK(!sample.dit_pressed && sample.dah_pressed && !sample.straight_pressed);
    keyin_close(&keyin);

    config.key_in_mode = KEYER_KEY_IN_SK_T;
    CHECK(keyin_open(&keyin, &DIGITAL_IO, &config) == MINI_OK);
    s_lines[13].level = 0u;
    s_lines[15].level = 1u;
    CHECK(keyin_read(&keyin, &sample) == MINI_OK);
    CHECK(!sample.dit_pressed && !sample.dah_pressed && sample.straight_pressed);
    CHECK(keyin_engine_mode(config.key_in_mode) == KEYER_ENGINE_INPUT_STRAIGHT);
    keyin_close(&keyin);

    config.key_in_mode = KEYER_KEY_IN_SK_R;
    CHECK(keyin_open(&keyin, &DIGITAL_IO, &config) == MINI_OK);
    s_lines[13].level = 1u;
    s_lines[15].level = 0u;
    CHECK(keyin_read(&keyin, &sample) == MINI_OK);
    CHECK(sample.straight_pressed);
    keyin_close(&keyin);
}

static void test_keyout_sk_and_release(void)
{
    keyout_t keyout;
    keyer_config_t config = default_config();

    reset_fake();
    CHECK(keyout_open(&keyout, &DIGITAL_IO, &config) == MINI_OK);
    CHECK(s_lines[3].mode == MINI_DIGITAL_IO_MODE_OUTPUT_OPEN_DRAIN);
    CHECK(s_lines[6].mode == MINI_DIGITAL_IO_MODE_OUTPUT_OPEN_DRAIN);
    CHECK(s_lines[3].level == 1u && s_lines[6].level == 1u);

    CHECK(keyout_apply(&keyout, true, KEYER_ENGINE_ELEMENT_DIT,
                       KEYER_ENGINE_INPUT_PADDLE) == MINI_OK);
    CHECK(s_lines[3].level == 0u && s_lines[6].level == 0u);
    CHECK(keyout_apply(&keyout, false, KEYER_ENGINE_ELEMENT_DIT,
                       KEYER_ENGINE_INPUT_PADDLE) == MINI_OK);
    CHECK(s_lines[3].level == 1u && s_lines[6].level == 1u);

    keyout_close(&keyout);
    CHECK(!s_lines[3].opened && !s_lines[6].opened);
    CHECK(s_lines[3].level == 1u && s_lines[6].level == 1u);
}

static void test_keyout_paddle_and_reverse(void)
{
    keyout_t keyout;
    keyer_config_t config = default_config();

    reset_fake();
    config.key_out_mode = KEYER_KEY_OUT_PADDLE;
    CHECK(keyout_open(&keyout, &DIGITAL_IO, &config) == MINI_OK);
    CHECK(keyout_apply(&keyout, true, KEYER_ENGINE_ELEMENT_DIT,
                       KEYER_ENGINE_INPUT_PADDLE) == MINI_OK);
    CHECK(s_lines[3].level == 0u && s_lines[6].level == 1u);
    CHECK(keyout_apply(&keyout, false, KEYER_ENGINE_ELEMENT_DIT,
                       KEYER_ENGINE_INPUT_PADDLE) == MINI_OK);
    CHECK(keyout_apply(&keyout, true, KEYER_ENGINE_ELEMENT_DAH,
                       KEYER_ENGINE_INPUT_PADDLE) == MINI_OK);
    CHECK(s_lines[3].level == 1u && s_lines[6].level == 0u);
    keyout_close(&keyout);

    reset_fake();
    config.key_out_mode = KEYER_KEY_OUT_PADDLE_R;
    CHECK(keyout_open(&keyout, &DIGITAL_IO, &config) == MINI_OK);
    CHECK(keyout_apply(&keyout, true, KEYER_ENGINE_ELEMENT_DIT,
                       KEYER_ENGINE_INPUT_PADDLE) == MINI_OK);
    CHECK(s_lines[3].level == 1u && s_lines[6].level == 0u);
    keyout_close(&keyout);
}

static void test_keyout_straight_and_sk_m(void)
{
    keyout_t keyout;
    keyer_config_t config = default_config();

    reset_fake();
    config.key_out_mode = KEYER_KEY_OUT_PADDLE;
    CHECK(keyout_open(&keyout, &DIGITAL_IO, &config) == MINI_OK);
    CHECK(keyout_apply(&keyout, true, KEYER_ENGINE_ELEMENT_NONE,
                       KEYER_ENGINE_INPUT_STRAIGHT) == MINI_OK);
    CHECK(s_lines[3].level == 0u && s_lines[6].level == 0u);
    keyout_close(&keyout);

    reset_fake();
    config.key_out_mode = KEYER_KEY_OUT_SK_M;
    CHECK(keyout_open(&keyout, &DIGITAL_IO, &config) == MINI_OK);
    CHECK(s_lines[3].level == 1u && s_lines[6].level == 0u);
    CHECK(keyout_apply(&keyout, true, KEYER_ENGINE_ELEMENT_DIT,
                       KEYER_ENGINE_INPUT_PADDLE) == MINI_OK);
    CHECK(s_lines[3].level == 0u && s_lines[6].level == 0u);
    keyout_close(&keyout);
    CHECK(s_lines[3].level == 1u && s_lines[6].level == 1u);
}

int main(void)
{
    test_defaults();
    test_keyin_modes();
    test_keyout_sk_and_release();
    test_keyout_paddle_and_reverse();
    test_keyout_straight_and_sk_m();
    puts("keyer_k4_io_test: PASS");
    return 0;
}

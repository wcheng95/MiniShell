#include "keyin.h"

#include <stddef.h>

static void clear_keyin(keyin_t *keyin)
{
    if (keyin == NULL) return;
    keyin->digital_io = NULL;
    keyin->tip = MINI_DIGITAL_IO_INVALID;
    keyin->ring = MINI_DIGITAL_IO_INVALID;
    keyin->mode = KEYER_KEY_IN_PADDLE;
}

keyer_engine_input_mode_t keyin_engine_mode(keyer_key_in_mode_t mode)
{
    return (mode == KEYER_KEY_IN_SK_T || mode == KEYER_KEY_IN_SK_R)
               ? KEYER_ENGINE_INPUT_STRAIGHT
               : KEYER_ENGINE_INPUT_PADDLE;
}

mini_result_t keyin_open(keyin_t *keyin,
                         const mini_digital_io_api_t *digital_io,
                         const keyer_config_t *config)
{
    mini_digital_io_config_t line_config;
    mini_result_t rc;

    if (keyin == NULL || digital_io == NULL || config == NULL ||
        digital_io->open == NULL || digital_io->read == NULL || digital_io->close == NULL) {
        return MINI_ERR_INVALID;
    }

    clear_keyin(keyin);
    keyin->digital_io = digital_io;
    keyin->mode = config->key_in_mode;

    line_config.struct_size = sizeof(line_config);
    line_config.mode = MINI_DIGITAL_IO_MODE_INPUT_PULLUP;
    line_config.initial_level = 1u;

    line_config.line_id = config->key_in_tip_line;
    rc = digital_io->open(&line_config, &keyin->tip);
    if (rc != MINI_OK) {
        clear_keyin(keyin);
        return rc;
    }

    line_config.line_id = config->key_in_ring_line;
    rc = digital_io->open(&line_config, &keyin->ring);
    if (rc != MINI_OK) {
        (void)digital_io->close(keyin->tip);
        clear_keyin(keyin);
        return rc;
    }

    return MINI_OK;
}

mini_result_t keyin_read(keyin_t *keyin, keyin_sample_t *out_sample)
{
    uint32_t tip_level;
    uint32_t ring_level;
    bool tip_pressed;
    bool ring_pressed;
    mini_result_t rc;

    if (keyin == NULL || out_sample == NULL || keyin->digital_io == NULL ||
        keyin->tip == MINI_DIGITAL_IO_INVALID || keyin->ring == MINI_DIGITAL_IO_INVALID) {
        return MINI_ERR_NOT_READY;
    }

    rc = keyin->digital_io->read(keyin->tip, &tip_level);
    if (rc != MINI_OK) return rc;
    rc = keyin->digital_io->read(keyin->ring, &ring_level);
    if (rc != MINI_OK) return rc;

    tip_pressed = tip_level == 0u;
    ring_pressed = ring_level == 0u;

    out_sample->dit_pressed = false;
    out_sample->dah_pressed = false;
    out_sample->straight_pressed = false;

    switch (keyin->mode) {
    case KEYER_KEY_IN_PADDLE:
        out_sample->dit_pressed = tip_pressed;
        out_sample->dah_pressed = ring_pressed;
        break;
    case KEYER_KEY_IN_PADDLE_R:
        out_sample->dit_pressed = ring_pressed;
        out_sample->dah_pressed = tip_pressed;
        break;
    case KEYER_KEY_IN_SK_T:
        out_sample->straight_pressed = tip_pressed;
        break;
    case KEYER_KEY_IN_SK_R:
        out_sample->straight_pressed = ring_pressed;
        break;
    default:
        return MINI_ERR_INVALID;
    }

    return MINI_OK;
}

void keyin_close(keyin_t *keyin)
{
    if (keyin == NULL || keyin->digital_io == NULL) return;

    if (keyin->ring != MINI_DIGITAL_IO_INVALID) {
        (void)keyin->digital_io->close(keyin->ring);
    }
    if (keyin->tip != MINI_DIGITAL_IO_INVALID) {
        (void)keyin->digital_io->close(keyin->tip);
    }
    clear_keyin(keyin);
}

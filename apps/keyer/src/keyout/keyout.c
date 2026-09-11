#include "keyout.h"

#include <stddef.h>

#define KEYOUT_ACTIVE_LEVEL   0u
#define KEYOUT_RELEASE_LEVEL  1u

static void clear_keyout(keyout_t *keyout)
{
    if (keyout == NULL) return;
    keyout->digital_io = NULL;
    keyout->tip = MINI_DIGITAL_IO_INVALID;
    keyout->ring = MINI_DIGITAL_IO_INVALID;
    keyout->mode = KEYER_KEY_OUT_OFF;
    keyout->tip_level = KEYOUT_RELEASE_LEVEL;
    keyout->ring_level = KEYOUT_RELEASE_LEVEL;
}

static mini_result_t write_level(keyout_t *keyout,
                                 mini_digital_io_t line,
                                 uint32_t *cached,
                                 uint32_t level)
{
    if (*cached == level) return MINI_OK;
    mini_result_t rc = keyout->digital_io->write(line, level);
    if (rc == MINI_OK) *cached = level;
    return rc;
}

static mini_result_t apply_levels(keyout_t *keyout,
                                  uint32_t tip_level,
                                  uint32_t ring_level)
{
    mini_result_t rc;

    if (keyout == NULL || keyout->digital_io == NULL) return MINI_ERR_NOT_READY;

    rc = write_level(keyout, keyout->tip, &keyout->tip_level, tip_level);
    if (rc != MINI_OK) return rc;
    rc = write_level(keyout, keyout->ring, &keyout->ring_level, ring_level);
    if (rc != MINI_OK) {
        (void)write_level(keyout, keyout->tip, &keyout->tip_level, KEYOUT_RELEASE_LEVEL);
        return rc;
    }
    return MINI_OK;
}

static mini_result_t apply_idle(keyout_t *keyout)
{
    if (keyout->mode == KEYER_KEY_OUT_SK_M) {
        return apply_levels(keyout, KEYOUT_RELEASE_LEVEL, KEYOUT_ACTIVE_LEVEL);
    }
    return apply_levels(keyout, KEYOUT_RELEASE_LEVEL, KEYOUT_RELEASE_LEVEL);
}

mini_result_t keyout_open(keyout_t *keyout,
                          const mini_digital_io_api_t *digital_io,
                          const keyer_config_t *config)
{
    mini_digital_io_config_t line_config;
    mini_result_t rc;

    if (keyout == NULL || digital_io == NULL || config == NULL ||
        digital_io->open == NULL || digital_io->write == NULL || digital_io->close == NULL) {
        return MINI_ERR_INVALID;
    }

    clear_keyout(keyout);
    keyout->digital_io = digital_io;
    keyout->mode = config->key_out_mode;

    if (keyout->mode == KEYER_KEY_OUT_OFF) return MINI_OK;

    line_config.struct_size = sizeof(line_config);
    line_config.mode = MINI_DIGITAL_IO_MODE_OUTPUT_OPEN_DRAIN;
    line_config.initial_level = KEYOUT_RELEASE_LEVEL;

    line_config.line_id = config->key_out_tip_line;
    rc = digital_io->open(&line_config, &keyout->tip);
    if (rc != MINI_OK) {
        clear_keyout(keyout);
        return rc;
    }

    line_config.line_id = config->key_out_ring_line;
    rc = digital_io->open(&line_config, &keyout->ring);
    if (rc != MINI_OK) {
        (void)digital_io->write(keyout->tip, KEYOUT_RELEASE_LEVEL);
        (void)digital_io->close(keyout->tip);
        clear_keyout(keyout);
        return rc;
    }

    keyout->tip_level = KEYOUT_RELEASE_LEVEL;
    keyout->ring_level = KEYOUT_RELEASE_LEVEL;
    rc = apply_idle(keyout);
    if (rc != MINI_OK) {
        keyout_close(keyout);
        return rc;
    }
    return MINI_OK;
}

mini_result_t keyout_apply(keyout_t *keyout,
                           bool key_down,
                           keyer_engine_element_t element,
                           keyer_engine_input_mode_t input_mode)
{
    bool tip_active = false;
    bool ring_active = false;

    if (keyout == NULL) return MINI_ERR_INVALID;
    if (keyout->mode == KEYER_KEY_OUT_OFF) return MINI_OK;
    if (keyout->digital_io == NULL || keyout->tip == MINI_DIGITAL_IO_INVALID ||
        keyout->ring == MINI_DIGITAL_IO_INVALID) return MINI_ERR_NOT_READY;

    if (!key_down) return apply_idle(keyout);

    if (input_mode == KEYER_ENGINE_INPUT_STRAIGHT) {
        switch (keyout->mode) {
        case KEYER_KEY_OUT_PADDLE:
        case KEYER_KEY_OUT_PADDLE_R:
        case KEYER_KEY_OUT_SK:
        case KEYER_KEY_OUT_SK_M:
            tip_active = true;
            ring_active = true;
            break;
        case KEYER_KEY_OUT_OFF:
        default:
            break;
        }
    } else {
        switch (keyout->mode) {
        case KEYER_KEY_OUT_PADDLE:
            tip_active = element == KEYER_ENGINE_ELEMENT_DIT;
            ring_active = element == KEYER_ENGINE_ELEMENT_DAH;
            break;
        case KEYER_KEY_OUT_PADDLE_R:
            tip_active = element == KEYER_ENGINE_ELEMENT_DAH;
            ring_active = element == KEYER_ENGINE_ELEMENT_DIT;
            break;
        case KEYER_KEY_OUT_SK:
        case KEYER_KEY_OUT_SK_M:
            tip_active = true;
            ring_active = true;
            break;
        case KEYER_KEY_OUT_OFF:
        default:
            break;
        }
    }

    return apply_levels(keyout,
                        tip_active ? KEYOUT_ACTIVE_LEVEL : KEYOUT_RELEASE_LEVEL,
                        ring_active ? KEYOUT_ACTIVE_LEVEL : KEYOUT_RELEASE_LEVEL);
}

mini_result_t keyout_release(keyout_t *keyout)
{
    if (keyout == NULL) return MINI_ERR_INVALID;
    if (keyout->mode == KEYER_KEY_OUT_OFF || keyout->digital_io == NULL) return MINI_OK;
    return apply_levels(keyout, KEYOUT_RELEASE_LEVEL, KEYOUT_RELEASE_LEVEL);
}

void keyout_close(keyout_t *keyout)
{
    if (keyout == NULL || keyout->digital_io == NULL) return;

    (void)keyout_release(keyout);
    if (keyout->ring != MINI_DIGITAL_IO_INVALID) {
        (void)keyout->digital_io->close(keyout->ring);
    }
    if (keyout->tip != MINI_DIGITAL_IO_INVALID) {
        (void)keyout->digital_io->close(keyout->tip);
    }
    clear_keyout(keyout);
}

#include "app_controller.h"

#include <stdbool.h>
#include <stddef.h>

#include "config_service.h"
#include "keyer_engine.h"
#include "keyer_types.h"
#include "keyin.h"
#include "keyout.h"

static const mini_api_t *s_api;
static keyer_config_t s_config;
static keyer_engine_t s_engine;
static keyin_t s_keyin;
static keyout_t s_keyout;
static bool s_initialized;

static void console_write(const char *text)
{
    if (s_api != NULL && s_api->console != NULL && s_api->console->write != NULL) {
        s_api->console->write(text);
    }
}

static bool exit_requested(void)
{
    mini_key_event_t event;

    if (s_api == NULL || s_api->input == NULL || s_api->input->key == NULL ||
        s_api->input->key->read == NULL) {
        return false;
    }

    event.struct_size = sizeof(event);
    event.type = MINI_KEY_EVENT_NONE;
    event.codepoint = 0u;
    event.key = MINI_KEY_NONE;
    event.modifiers = MINI_KEY_MOD_NONE;

    while (s_api->input->key->read(&event, MINI_WAIT_NONE) == MINI_OK) {
        if (event.type == MINI_KEY_EVENT_CHAR &&
            (event.codepoint == (uint32_t)'q' || event.codepoint == (uint32_t)'Q')) {
            return true;
        }
        if (event.type == MINI_KEY_EVENT_SPECIAL && event.key == MINI_KEY_ESCAPE) {
            return true;
        }
        event.struct_size = sizeof(event);
    }

    return false;
}

static void emit_decoded_events(void)
{
    keyer_engine_event_t event;
    char text[2] = {'\0', '\0'};

    while (keyer_engine_poll_event(&s_engine, &event)) {
        switch (event.type) {
        case KEYER_ENGINE_EVENT_CHAR:
            text[0] = event.ch;
            console_write(text);
            break;
        case KEYER_ENGINE_EVENT_WORD_SPACE:
            console_write(" ");
            break;
        case KEYER_ENGINE_EVENT_BACKSPACE:
            console_write("<BS>");
            break;
        case KEYER_ENGINE_EVENT_ENTER:
            console_write("\n");
            break;
        case KEYER_ENGINE_EVENT_DIT:
        case KEYER_ENGINE_EVENT_DAH:
        case KEYER_ENGINE_EVENT_NONE:
        default:
            break;
        }
    }
}

mini_result_t app_controller_init(const mini_api_t *api)
{
    keyer_engine_config_t engine_config;
    bool loaded_from_file = false;
    mini_result_t rc;

    if (api == NULL || api->api_version != MINISHELL_API_VERSION ||
        api->digital_io == NULL || api->time_location == NULL ||
        api->time_location->monotonic_us == NULL || api->time_location->sleep_ms == NULL) {
        return MINI_ERR_NOT_READY;
    }

    s_api = api;
    s_initialized = false;

    rc = config_service_load(api, &s_config, &loaded_from_file);
    if (rc != MINI_OK) {
        console_write("keyer K4: invalid/unreadable setting.txt\n");
        s_api = NULL;
        return rc;
    }

    rc = keyin_open(&s_keyin, api->digital_io, &s_config);
    if (rc != MINI_OK) {
        console_write("keyer K4: KeyIn open failed\n");
        s_api = NULL;
        return rc;
    }

    rc = keyout_open(&s_keyout, api->digital_io, &s_config);
    if (rc != MINI_OK) {
        console_write("keyer K4: KeyOut open failed\n");
        keyin_close(&s_keyin);
        s_api = NULL;
        return rc;
    }

    engine_config.wpm = s_config.wpm;
    engine_config.input_mode = keyin_engine_mode(s_config.key_in_mode);
    engine_config.paddle_mode = s_config.paddle_mode;
    keyer_engine_init(&s_engine, &engine_config, api->time_location->monotonic_us());

    if (loaded_from_file) {
        console_write("keyer K4: loaded /flash/keyer/setting.txt\n");
    } else {
        console_write("keyer K4: using default settings\n");
    }
    console_write("keyer K4: ready; q or ESC exits\n");
    s_initialized = true;
    return MINI_OK;
}

int app_controller_run(void)
{
    keyin_sample_t sample;
    keyer_engine_input_mode_t input_mode;

    if (!s_initialized || s_api == NULL) return 2;
    input_mode = keyin_engine_mode(s_config.key_in_mode);

    while (!exit_requested()) {
        mini_result_t rc = keyin_read(&s_keyin, &sample);
        if (rc != MINI_OK) {
            console_write("\nkeyer K4: KeyIn read failed\n");
            (void)keyout_release(&s_keyout);
            return 3;
        }

        uint64_t now_us = s_api->time_location->monotonic_us();
        keyer_engine_step(&s_engine,
                          now_us,
                          sample.dit_pressed,
                          sample.dah_pressed,
                          sample.straight_pressed);

        rc = keyout_apply(&s_keyout,
                          keyer_engine_key_down(&s_engine),
                          keyer_engine_last_element(&s_engine),
                          input_mode);
        if (rc != MINI_OK) {
            console_write("\nkeyer K4: KeyOut write failed\n");
            (void)keyout_release(&s_keyout);
            return 4;
        }

        emit_decoded_events();
        (void)s_api->time_location->sleep_ms(1u);
    }

    console_write("\nkeyer K4: exit\n");
    return 0;
}

void app_controller_shutdown(void)
{
    if (!s_initialized && s_api == NULL) return;

    keyout_close(&s_keyout);
    keyin_close(&s_keyin);
    s_initialized = false;
    s_api = NULL;
}

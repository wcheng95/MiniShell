#include "app_controller.h"

#include <stdbool.h>
#include <stddef.h>

#include "config_service.h"
#include "keyer_engine.h"
#include "keyer_types.h"
#include "keyin.h"
#include "keyout.h"
#include "sidetone.h"
#include "tx_engine.h"
#include "ui_shell.h"
#include "ui_adapter.h"

static const mini_api_t *s_api;
static keyer_config_t s_config;
static keyer_engine_t s_engine;
static keyin_t s_keyin;
static keyout_t s_keyout;
static sidetone_t s_sidetone;
static bool s_initialized;
static tx_engine_t s_tx;
static ui_shell_t s_ui;
static ui_adapter_t s_adapter;

static void console_write(const char *text)
{
    if (s_api != NULL && s_api->console != NULL && s_api->console->write != NULL) {
        s_api->console->write(text);
    }
}

static void emit_decoded_events(void)
{
    keyer_engine_event_t event;

    while (keyer_engine_poll_event(&s_engine, &event)) {
        switch (event.type) {
        case KEYER_ENGINE_EVENT_CHAR:
            ui_shell_history(&s_ui, event.ch);
            break;
        case KEYER_ENGINE_EVENT_WORD_SPACE:
            ui_shell_history(&s_ui, ' ');
            break;
        case KEYER_ENGINE_EVENT_BACKSPACE:
            ui_shell_history(&s_ui, '\b');
            break;
        case KEYER_ENGINE_EVENT_ENTER:
            ui_shell_history(&s_ui, '\n');
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
        console_write("keyer: invalid/unreadable setting.txt\n");
        s_api = NULL;
        return rc;
    }

    rc = keyin_open(&s_keyin, api->digital_io, &s_config);
    if (rc != MINI_OK) {
        console_write("keyer: KeyIn open failed\n");
        s_api = NULL;
        return rc;
    }

    rc = keyout_open(&s_keyout, api->digital_io, &s_config);
    if (rc != MINI_OK) {
        console_write("keyer: KeyOut open failed\n");
        keyin_close(&s_keyin);
        s_api = NULL;
        return rc;
    }

    engine_config.wpm = s_config.wpm;
    engine_config.input_mode = keyin_engine_mode(s_config.key_in_mode);
    engine_config.paddle_mode = s_config.paddle_mode;
    keyer_engine_init(&s_engine, &engine_config, api->time_location->monotonic_us());

    rc = sidetone_open(&s_sidetone, api->audio,
                       s_config.sidetone_enabled, s_config.sidetone_hz);
    if (rc == MINI_ERR_UNSUPPORTED) {
        console_write("keyer: sidetone unavailable; continuing silent\n");
    } else if (rc != MINI_OK) {
        console_write("keyer: sidetone open failed\n");
        keyout_close(&s_keyout);
        keyin_close(&s_keyin);
        s_api = NULL;
        return rc;
    }

    (void)loaded_from_file;
    tx_engine_init(&s_tx);
    ui_shell_init(&s_ui);
    sidetone_settings(&s_sidetone, s_config.sidetone_hz, s_config.volume, s_config.mute);
    rc = ui_adapter_open(&s_adapter, api);
    if (rc != MINI_OK) {
        sidetone_close(&s_sidetone); keyout_close(&s_keyout); keyin_close(&s_keyin);
        ui_adapter_close(&s_adapter); s_api = NULL; return rc;
    }
    s_initialized = true;
    return MINI_OK;
}

/* Small bounded remainder avoids a resident libgcc dependency in the ELF. */
static int utc_minutes(int64_t seconds)
{
    bool negative = seconds < 0;
    uint64_t magnitude = negative ? 0u - (uint64_t)seconds : (uint64_t)seconds;
    uint32_t remainder = 0;
    for (int bit = 63; bit >= 0; --bit) {
        remainder = remainder * 2u + (uint32_t)((magnitude >> bit) & 1u);
        if (remainder >= 86400u) remainder -= 86400u;
    }
    if (negative && remainder) remainder = 86400u - remainder;
    return (int)(remainder / 60u);
}

static mini_result_t render(uint64_t now)
{
    mini_utc_time_t utc = {.struct_size = sizeof(utc)};
    int minutes = -1;
    if (s_api->time_location->utc_get && s_api->time_location->utc_get(&utc) == MINI_OK) {
        minutes = utc_minutes(utc.unix_seconds);
    }
    ui_frame_t frame;
    ui_shell_render(&s_ui, &s_config, minutes, s_tx.fifo, s_tx.tune, now, &frame);
    return ui_adapter_present(&s_adapter, &frame);
}

static mini_result_t apply_config(const keyer_config_t *old, uint64_t now)
{
    mini_result_t rc;
    if (old->key_out_mode != s_config.key_out_mode) {
        keyout_close(&s_keyout);
        rc = keyout_open(&s_keyout, s_api->digital_io, &s_config);
        if (rc != MINI_OK) return rc;
    }
    if (old->key_in_mode != s_config.key_in_mode) {
        s_keyin.mode = s_config.key_in_mode;
        keyer_engine_set_input_mode(&s_engine, keyin_engine_mode(s_config.key_in_mode), now);
    }
    if (old->paddle_mode != s_config.paddle_mode)
        keyer_engine_set_paddle_mode(&s_engine, s_config.paddle_mode, now);
    if (old->wpm != s_config.wpm) keyer_engine_set_wpm(&s_engine, s_config.wpm);
    sidetone_settings(&s_sidetone, s_config.sidetone_hz, s_config.volume, s_config.mute);
    rc = config_service_save(s_api, &s_config);
    ui_shell_status(&s_ui, rc == MINI_OK ? "Saved" : "Save failed", now);
    if (rc != MINI_OK) console_write("keyer: setting.txt save failed; runtime setting retained\n");
    return MINI_OK;
}

int app_controller_run(void)
{
    if (!s_initialized || s_api == NULL) return 2;
    uint64_t next_render = 0;
    for (;;) {
        uint64_t now = s_api->time_location->monotonic_us();
        /* Bound input work per tick so even a flooded keyboard cannot starve
         * physical sampling. The physical sample below always wins arbitration. */
        ui_input_t input;
        if (ui_adapter_read(&s_adapter, &input)) {
            keyer_config_t old = s_config;
            ui_result_t action = ui_shell_input(&s_ui, &s_config, input);
            tx_result_t txrc = TX_OK;
            switch (action.action) {
            case UI_ACT_QUIT: return 0;
            case UI_ACT_SAVE:
                if (apply_config(&old, now) != MINI_OK) return 4;
                break;
            case UI_ACT_TEXT: {
                char text[2] = {action.ch, 0};
                txrc = tx_engine_append(&s_tx, text, now); break;
            }
            case UI_ACT_MEMORY: txrc = tx_engine_memory(&s_tx, &s_config, action.memory, now); break;
            case UI_ACT_TUNE: tx_engine_tune(&s_tx, now); break;
            case UI_ACT_CANCEL: tx_engine_cancel(&s_tx); break;
            case UI_ACT_START: tx_engine_start(&s_tx); break;
            case UI_ACT_BACKSPACE: tx_engine_backspace(&s_tx); break;
            default: break;
            }
            if (txrc != TX_OK) ui_shell_status(&s_ui, txrc == TX_FULL ? "TX full" : "Unsupported char", now);
            next_render = 0;
        }
        keyin_sample_t sample;
        if (keyin_read(&s_keyin, &sample) != MINI_OK) {
            console_write("keyer: KeyIn read failed\n");
            (void)keyout_release(&s_keyout); return 3;
        }
        now = s_api->time_location->monotonic_us();
        bool physical = sample.dit_pressed || sample.dah_pressed || sample.straight_pressed;
        if (physical) tx_engine_cancel(&s_tx);
        keyer_engine_step(&s_engine, now, sample.dit_pressed, sample.dah_pressed, sample.straight_pressed);
        bool manual = keyer_engine_key_down(&s_engine);
        bool automatic = tx_engine_step(&s_tx, &s_config, now, physical || manual);
        bool down = manual || automatic;
        if (keyout_apply(&s_keyout, down, keyer_engine_last_element(&s_engine),
                         keyin_engine_mode(s_config.key_in_mode)) != MINI_OK) {
            console_write("keyer: KeyOut write failed\n");
            (void)keyout_release(&s_keyout); return 4;
        }
        if (sidetone_apply(&s_sidetone, down) != MINI_OK) {
            console_write("keyer: sidetone write failed\n");
            (void)keyout_release(&s_keyout); return 5;
        }
        emit_decoded_events();
        if (now >= next_render) {
            if (render(now) != MINI_OK) { (void)keyout_release(&s_keyout); return 6; }
            next_render = now + 50000u;
        }
        if (!sidetone_streaming(&s_sidetone)) (void)s_api->time_location->sleep_ms(1u);
    }
}

void app_controller_shutdown(void)
{
    if (!s_initialized && s_api == NULL) return;

    tx_engine_cancel(&s_tx);
    keyout_close(&s_keyout);
    sidetone_close(&s_sidetone);
    ui_adapter_close(&s_adapter);
    keyin_close(&s_keyin);
    s_initialized = false;
    s_api = NULL;
}

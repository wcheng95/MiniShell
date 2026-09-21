#include "app_core.h"
#include "minicw_port.h"
#include "audio_service.h"
#include "storage_service.h"
#include "keyer_service.h"
#include "ui_service.h"
#include "minicw_ascii.h"
#include "minicw_libc.h"
#include <string.h>
static uint32_t app_core_ms_to_delay_ticks(uint32_t ms)
{
    uint32_t ticks = minicw_ticks_from_ms(ms);
    return ticks > 0 ? ticks : 1;
}

static bool app_core_tick_reached(uint32_t now, uint32_t due)
{
    return (uint32_t)(now - due) < (uint32_t)(UINT32_MAX / 2U);
}

typedef struct {
    bool tx_pending;
    uint32_t tx_due;

    bool m1_repeat_active;
    bool m1_repeat_waiting;
    uint32_t m1_repeat_due;
    bool last_append_was_message;
    uint32_t tx_revision;

    bool tune_active;
    bool tune_timeout_pending;
    uint32_t tune_timeout_due;
    bool tune_last_latched;
    bool tune_last_output_active;

} app_keyer_state_t;
static app_keyer_state_t s_keyer;

/* Only app_core decides when filesystem work is safe. Storage never drives CW. */
static storage_snapshot_t s_settings;
static bool s_settings_dirty, s_save_failed;
static uint32_t s_quiet_since;
static void app_core_snapshot(storage_snapshot_t *out)
{
    out->volume = audio_service_get_volume();
    out->tone_hz = audio_service_get_tone_hz();
    out->key_in = keyer_service_get_key_in_mode();
    out->key_in_wpm = keyer_service_get_key_in_wpm();
    keyer_service_get_config_copy(&out->keyer);
}
static void app_core_settings_changed(void)
{
    storage_snapshot_t current;
    app_core_snapshot(&current);
    if (!storage_equal(&current, &s_settings)) {
        s_settings = current;
        s_settings_dirty = true;
        s_save_failed = false;
        s_quiet_since = minicw_port_now_ms();
    }
}
static void app_core_save_settings(void)
{
    if (storage_save(&s_settings)) {
        s_settings_dirty = false;
    } else {
        s_save_failed = true;
        ui_service_keyer_set_status("Save failed");
        ui_service_refresh();
    }
}
static void app_core_persistence_update(void)
{
    app_core_settings_changed();
    if (!s_settings_dirty || s_save_failed) return;
    /* Raw inactive pins also exclude muted straight-key holds. Audio busy alone
     * cannot detect those; this does not alter the Keyer input state machine. */
    if (s_keyer.tx_pending || keyer_service_is_tx_active() || keyer_service_tx_has_text() ||
        s_keyer.m1_repeat_active || s_keyer.m1_repeat_waiting || s_keyer.tune_active ||
        keyer_service_get_tune_output_active() || audio_service_is_busy() ||
        minicw_port_read(13U) == 0 || minicw_port_read(15U) == 0) {
        s_quiet_since = minicw_port_now_ms();
        return;
    }
    /* A quiet interval also outlasts the reference's release tail after busy
     * becomes false. No delay or blocking wait is added to Keyer scheduling. */
    if ((uint32_t)(minicw_port_now_ms() - s_quiet_since) >= 250U) app_core_save_settings();
}

static void app_core_keyer_set_tx_display(void)
{
    char tx_text[UI_INPUT_EVENT_TEXT_MAX + 1U];

    keyer_service_tx_copy_text(tx_text, sizeof(tx_text));
    ui_service_keyer_set_tx_text(tx_text);
}

static void app_core_keyer_sync_tx_display(bool force)
{
    uint32_t revision = keyer_service_tx_revision();

    if (force || revision != s_keyer.tx_revision) {
        s_keyer.tx_revision = revision;
        app_core_keyer_set_tx_display();
        ui_service_refresh();
    }
}

static void app_core_keyer_cancel_repeat(void)
{
    s_keyer.m1_repeat_active = false;
    s_keyer.m1_repeat_waiting = false;
}

static void app_core_keyer_clear_tx_fifo(void)
{
    s_keyer.tx_pending = false;
    s_keyer.last_append_was_message = false;
    app_core_keyer_cancel_repeat();
    keyer_service_tx_clear();
    app_core_keyer_sync_tx_display(true);
}

static void app_core_keyer_schedule_tx(void)
{
    uint8_t delay_s;

    if (keyer_service_is_tx_active()) {
        s_keyer.tx_pending = false;
        return;
    }

    delay_s = keyer_service_get_tx_delay_s();
    if (delay_s == 0U) {
        s_keyer.tx_pending = false;
        keyer_service_tx_start();
        app_core_keyer_sync_tx_display(false);
        return;
    }

    s_keyer.tx_pending = true;
    s_keyer.tx_due = minicw_port_ticks() + app_core_ms_to_delay_ticks((uint32_t)delay_s * 1000U);
}

static void app_core_keyer_start_tx_now(void)
{
    if (!keyer_service_tx_has_text()) {
        s_keyer.tx_pending = false;
        return;
    }

    s_keyer.tx_pending = false;
    keyer_service_tx_start();
    app_core_keyer_sync_tx_display(false);
}

static void app_core_keyer_append_tx_char(char key)
{
    char normalized = key == ' ' ? ' ' : (char)minicw_upper((unsigned char)key);
    char text[2] = {normalized, '\0'};
    bool insert_space;

    if (normalized != ' ' && audio_service_get_cw_pattern(normalized) == NULL) {
        ui_service_keyer_set_status("Unsupported");
        return;
    }

    app_core_keyer_cancel_repeat();
    insert_space = s_keyer.last_append_was_message && normalized != ' ';
    if (!keyer_service_tx_append_text(text, insert_space)) {
        ui_service_keyer_set_status("TX buffer full");
        return;
    }

    if (insert_space) {
        keyer_service_op_feed_char(' ');
    }
    keyer_service_op_feed_char(normalized);
    s_keyer.last_append_was_message = false;
    app_core_keyer_sync_tx_display(true);
    app_core_keyer_schedule_tx();
}

static void app_core_keyer_backspace_tx(void)
{
    if (keyer_service_tx_backspace()) {
        if (!keyer_service_tx_has_text()) {
            s_keyer.tx_pending = false;
            s_keyer.last_append_was_message = false;
        }
        app_core_keyer_sync_tx_display(true);
    }
}

static void app_core_keyer_append_message(uint8_t message_index)
{
    const char *message;
    bool insert_space;
    bool repeat_m1;

    if (message_index < 1U || message_index > KEYER_MESSAGE_COUNT) {
        return;
    }

    repeat_m1 = message_index == 1U;
    if (!repeat_m1) {
        app_core_keyer_cancel_repeat();
    }

    insert_space = keyer_service_tx_has_text();
    message = keyer_service_get_message((uint8_t)(message_index - 1U));
    if (!keyer_service_tx_append_text(message, insert_space)) {
        ui_service_keyer_set_status("TX buffer full");
        return;
    }

    if (insert_space) {
        keyer_service_op_feed_char(' ');
    }
    keyer_service_op_feed_text(message);
    if (repeat_m1) {
        s_keyer.m1_repeat_active = true;
        s_keyer.m1_repeat_waiting = false;
    }
    s_keyer.last_append_was_message = true;
    app_core_keyer_sync_tx_display(true);
    app_core_keyer_schedule_tx();
}

static void app_core_keyer_repeat_update(void)
{
    const char *message;

    if (!s_keyer.m1_repeat_active || s_keyer.tx_pending ||
        keyer_service_is_tx_active() || keyer_service_tx_has_text()) {
        if (keyer_service_is_tx_active() || keyer_service_tx_has_text() || s_keyer.tx_pending) {
            s_keyer.m1_repeat_waiting = false;
        }
        return;
    }

    if (!s_keyer.m1_repeat_waiting) {
        s_keyer.m1_repeat_waiting = true;
        s_keyer.m1_repeat_due =
            minicw_port_ticks() +
            app_core_ms_to_delay_ticks((uint32_t)keyer_service_get_repeat_interval_s() * 1000U);
        return;
    }

    if (!app_core_tick_reached(minicw_port_ticks(), s_keyer.m1_repeat_due)) {
        return;
    }

    s_keyer.m1_repeat_waiting = false;
    message = keyer_service_get_message(0U);
    if (!keyer_service_tx_append_text(message, false)) {
        ui_service_keyer_set_status("TX buffer full");
        return;
    }

    keyer_service_op_feed_text(message);
    s_keyer.last_append_was_message = true;
    app_core_keyer_sync_tx_display(true);
    app_core_keyer_start_tx_now();
}

static void app_core_keyer_sync_tune_ui(bool force)
{
    bool latched = s_keyer.tune_active && keyer_service_get_tune_latched();
    bool output_active = s_keyer.tune_active && keyer_service_get_tune_output_active();

    if (force || s_keyer.tune_last_latched != latched ||
        s_keyer.tune_last_output_active != output_active) {
        s_keyer.tune_last_latched = latched;
        s_keyer.tune_last_output_active = output_active;
        ui_service_refresh();
    }
}

static void app_core_keyer_set_tune_active(bool active)
{
    if (active) {
        app_core_keyer_clear_tx_fifo();
        s_keyer.tune_active = true;
        s_keyer.tune_timeout_pending = false;
        keyer_service_set_tune_active(true);
        ui_service_keyer_set_tune_active(true);
        app_core_keyer_sync_tune_ui(true);
        return;
    }

    s_keyer.tune_timeout_pending = false;
    keyer_service_set_tune_latched(false);
    keyer_service_set_tune_active(false);
    s_keyer.tune_active = false;
    ui_service_keyer_set_tune_active(false);
    app_core_keyer_sync_tune_ui(true);
}

static void app_core_keyer_set_tune_latched(bool latched)
{
    uint8_t timeout_s;

    if (!s_keyer.tune_active) {
        return;
    }

    keyer_service_set_tune_latched(latched);
    if (!latched) {
        s_keyer.tune_timeout_pending = false;
        app_core_keyer_sync_tune_ui(true);
        return;
    }

    timeout_s = keyer_service_get_tune_timeout_s();
    if (timeout_s == 0U) {
        s_keyer.tune_timeout_pending = false;
    } else {
        s_keyer.tune_timeout_pending = true;
        s_keyer.tune_timeout_due =
            minicw_port_ticks() + app_core_ms_to_delay_ticks((uint32_t)timeout_s * 1000U);
    }
    app_core_keyer_sync_tune_ui(true);
}

static void app_core_keyer_update(void)
{
    if (keyer_service_take_sk_wpm_save_request()) {
        ui_service_refresh();
    }

    if (s_keyer.tune_active) {
        if (s_keyer.tune_timeout_pending && !keyer_service_get_tune_latched()) {
            s_keyer.tune_timeout_pending = false;
        }

        if (s_keyer.tune_timeout_pending &&
            app_core_tick_reached(minicw_port_ticks(), s_keyer.tune_timeout_due)) {
            keyer_service_set_tune_latched(false);
            s_keyer.tune_timeout_pending = false;
        }

        app_core_keyer_sync_tune_ui(false);
        return;
    }

    if (s_keyer.tx_pending && app_core_tick_reached(minicw_port_ticks(), s_keyer.tx_due)) {
        app_core_keyer_start_tx_now();
    }

    app_core_keyer_repeat_update();
    app_core_keyer_sync_tx_display(false);
}

static void app_core_handle_volume_changed(const ui_input_event_t *event)
{
    if (event == NULL) {
        return;
    }

    audio_service_set_volume((uint8_t)event->value);
    audio_service_play_feedback_beep();
    ui_service_refresh();
}

static void app_core_handle_tone_changed(const ui_input_event_t *event)
{
    if (event == NULL) {
        return;
    }

    audio_service_set_tone_hz((uint16_t)event->value);
    audio_service_play_feedback_beep();
    ui_service_refresh();
}

static void app_core_handle_key_in_wpm_changed(const ui_input_event_t *event)
{
    if (event == NULL) {
        return;
    }

    keyer_service_set_key_in_wpm((uint8_t)event->value);
    ui_service_refresh();
}

static void app_core_handle_key_in_mode_changed(const ui_input_event_t *event)
{
    int direction = 1;

    if (event != NULL && event->delta != 0) {
        direction = event->delta;
    }

    keyer_service_cycle_key_in_mode(direction);
    ui_service_refresh();
}



static void app_core_handle_key_out_mode_changed(const ui_input_event_t *event)
{
    int direction = 1;

    if (event != NULL && event->delta != 0) {
        direction = event->delta;
    }

    keyer_service_cycle_key_out_mode(direction);
    ui_service_refresh();
}

static void app_core_handle_keyer_paddle_mode_changed(const ui_input_event_t *event)
{
    int direction = 1;

    if (event != NULL && event->delta != 0) {
        direction = event->delta;
    }

    keyer_service_cycle_paddle_mode(direction);
    ui_service_refresh();
}

static void app_core_handle_keyer_mute_changed(const ui_input_event_t *event)
{
    if (event != NULL && event->setting == UI_SETTING_KEYER_MUTE) {
        keyer_service_set_mute(event->value != 0);
    } else {
        keyer_service_toggle_mute();
    }
    ui_service_refresh();
}

static void app_core_handle_keyer_config_changed(const ui_input_event_t *event)
{
    keyer_config_t config;

    if (event == NULL) {
        return;
    }

    keyer_service_get_config_copy(&config);
    switch (event->setting) {
    case UI_SETTING_KEYER_TX_DELAY_S:
        config.tx_delay_s = (uint8_t)event->value;
        break;
    case UI_SETTING_KEYER_TUNE_TIMEOUT_S:
        config.tune_timeout_s = (uint8_t)event->value;
        break;
    case UI_SETTING_KEYER_REPEAT_INTERVAL_S:
        config.repeat_interval_s = (uint8_t)event->value;
        break;
    case UI_SETTING_KEYER_SK_WPM:
        config.sk_wpm = (uint8_t)event->value;
        break;
    case UI_SETTING_KEYER_MYCALL:
        snprintf(config.mycall,
                 sizeof(config.mycall),
                 "%.*s",
                 (int)KEYER_MYCALL_MAX_LEN,
                 event->text);
        break;
    case UI_SETTING_KEYER_MESSAGE_1:
    case UI_SETTING_KEYER_MESSAGE_2:
    case UI_SETTING_KEYER_MESSAGE_3:
    case UI_SETTING_KEYER_MESSAGE_4:
    case UI_SETTING_KEYER_MESSAGE_5: {
        uint8_t index = (uint8_t)(event->setting - UI_SETTING_KEYER_MESSAGE_1);
        snprintf(config.message[index],
                 sizeof(config.message[index]),
                 "%.*s",
                 (int)KEYER_MESSAGE_MAX_LEN,
                 event->text);
        break;
    }
    case UI_SETTING_NONE:
    case UI_SETTING_VOLUME:
    case UI_SETTING_TONE_HZ:
    case UI_SETTING_KEY_IN_WPM:
    case UI_SETTING_KEY_IN_MODE:
    case UI_SETTING_KEY_OUT_MODE:
    case UI_SETTING_KEYER_PADDLE_MODE:
    case UI_SETTING_KEYER_MUTE:
    default:
        break;
    }

    keyer_service_set_config(&config);
    ui_service_refresh();
}
static bool app_core_handle_keyer_mode_decoded_event(const keyer_event_t *event)
{
    if (event == NULL) {
        return false;
    }

    switch (event->type) {
    case KEYER_EVENT_CHAR_COMPLETE:
        keyer_service_op_feed_char(event->decoded_char);
        ui_service_keyer_append_decoded_char(event->decoded_char);
        return true;
    case KEYER_EVENT_WORD_SPACE:
        keyer_service_op_feed_char(' ');
        ui_service_keyer_append_decoded_char(' ');
        return true;
    case KEYER_EVENT_BACKSPACE:
        ui_service_keyer_backspace_decoded();
        return true;
    case KEYER_EVENT_ENTER:
        return false;
    case KEYER_EVENT_TX_CANCELLED:
        s_keyer.tx_pending = false;
        app_core_keyer_cancel_repeat();
        s_keyer.last_append_was_message = false;
        app_core_keyer_sync_tx_display(true);
        return true;
    case KEYER_EVENT_DIT:
    case KEYER_EVENT_DAH:
        app_core_keyer_cancel_repeat();
        return false;
    case KEYER_EVENT_NONE:
    default:
        break;
    }

    return false;
}


void app_core_init(void)
{
    memset(&s_keyer, 0, sizeof(s_keyer));
    storage_load_t loaded = storage_load(&s_settings);
    s_settings_dirty = s_save_failed = false;
    s_quiet_since = minicw_port_now_ms();
    /* All startup filesystem reads have finished before the frozen Tone open.
     * Apply the snapshot through existing setters, without changing that seam. */
    audio_service_init();
    keyer_service_init();
    if (loaded == STORAGE_OK) {
        audio_service_set_volume(s_settings.volume);
        audio_service_set_tone_hz(s_settings.tone_hz);
        keyer_service_set_key_in_mode(s_settings.key_in);
        keyer_service_set_key_in_wpm(s_settings.key_in_wpm);
        keyer_service_set_config(&s_settings.keyer);
    }
    app_core_snapshot(&s_settings);
    ui_service_init();
    if (loaded == STORAGE_INVALID) ui_service_keyer_set_status("Settings invalid");
    if (loaded == STORAGE_READ_FAILED) ui_service_keyer_set_status("Settings read failed");
    ui_service_show_demo_screen();
}

void app_core_step(void)
{
    keyer_service_update();
    keyer_event_t key;
    while ((key = keyer_service_poll_event()).type != KEYER_EVENT_NONE) {
        if (app_core_handle_keyer_mode_decoded_event(&key)) ui_service_refresh();
    }
    ui_input_event_t event = ui_service_poll_input();
    switch (event.type) {
    case UI_INPUT_EVENT_CANCEL:
        if (s_keyer.tune_active) app_core_keyer_set_tune_active(false);
        app_core_keyer_clear_tx_fifo();
        audio_service_stop_all();
        break;
    case UI_INPUT_EVENT_VOLUME_CHANGED: app_core_handle_volume_changed(&event); break;
    case UI_INPUT_EVENT_TONE_CHANGED: app_core_handle_tone_changed(&event); break;
    case UI_INPUT_EVENT_KEY_IN_WPM_CHANGED: app_core_handle_key_in_wpm_changed(&event); break;
    case UI_INPUT_EVENT_KEY_IN_MODE_CHANGED: app_core_handle_key_in_mode_changed(&event); break;
    case UI_INPUT_EVENT_KEY_OUT_MODE_CHANGED: app_core_handle_key_out_mode_changed(&event); break;
    case UI_INPUT_EVENT_KEYER_PADDLE_MODE_CHANGED: app_core_handle_keyer_paddle_mode_changed(&event); break;
    case UI_INPUT_EVENT_KEYER_CONFIG_CHANGED: app_core_handle_keyer_config_changed(&event); break;
    case UI_INPUT_EVENT_KEYER_MUTE_CHANGED: app_core_handle_keyer_mute_changed(&event); break;
    case UI_INPUT_EVENT_KEYER_MACRO_SELECTED: app_core_keyer_append_message((uint8_t)event.value); break;
    case UI_INPUT_EVENT_KEYER_CLEAR:
        app_core_keyer_clear_tx_fifo();
        ui_service_keyer_clear_decoded();
        break;
    case UI_INPUT_EVENT_KEYER_TUNE_CHANGED: app_core_keyer_set_tune_active(event.value != 0); break;
    case UI_INPUT_EVENT_KEYER_TUNE_LATCH_CHANGED: app_core_keyer_set_tune_latched(event.value != 0); break;
    case UI_INPUT_EVENT_SELECT: app_core_keyer_start_tx_now(); break;
    case UI_INPUT_EVENT_CHAR_INPUT: app_core_keyer_append_tx_char(event.key); break;
    case UI_INPUT_EVENT_BACKSPACE: app_core_keyer_backspace_tx(); break;
    default: break;
    }
    if (event.type != UI_INPUT_EVENT_NONE) ui_service_refresh();
    app_core_keyer_update();
    app_core_persistence_update();
}

void app_core_shutdown(void)
{
    app_core_settings_changed();
    app_core_keyer_cancel_repeat();
    keyer_service_set_tune_active(false);
    keyer_service_tx_clear();
    keyer_service_set_key_out_mode(KEYER_KEY_OUT_OFF);
    audio_service_stop_all();
}

/* Called after Tone close, while Filesystem and other app resources still live. */
void app_core_save_on_exit(void)
{
    if (s_settings_dirty) app_core_save_settings();
}

/* Mini-CW Keyer UI state and events. ui_screen maps frames to MiniShell text
 * Display; minicw_input maps logical Input events. app_core applies settings. */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UI_INPUT_EVENT_NONE = 0,
    UI_INPUT_EVENT_CANCEL,
    UI_INPUT_EVENT_SELECT,
    UI_INPUT_EVENT_CHAR_INPUT,
    UI_INPUT_EVENT_BACKSPACE,
    UI_INPUT_EVENT_VOLUME_CHANGED,
    UI_INPUT_EVENT_TONE_CHANGED,
    UI_INPUT_EVENT_KEY_IN_WPM_CHANGED,
    UI_INPUT_EVENT_KEY_IN_MODE_CHANGED,
    UI_INPUT_EVENT_KEY_OUT_MODE_CHANGED,
    UI_INPUT_EVENT_KEYER_PADDLE_MODE_CHANGED,
    UI_INPUT_EVENT_KEYER_CONFIG_CHANGED,
    UI_INPUT_EVENT_KEYER_MUTE_CHANGED,
    UI_INPUT_EVENT_KEYER_MACRO_SELECTED,
    UI_INPUT_EVENT_KEYER_SHORTCUT_CHANGED,
    UI_INPUT_EVENT_KEYER_CLEAR,
    UI_INPUT_EVENT_KEYER_TUNE_CHANGED,
    UI_INPUT_EVENT_KEYER_TUNE_LATCH_CHANGED,
} ui_input_event_type_t;

typedef enum {
    UI_SETTING_NONE = 0,
    UI_SETTING_VOLUME,
    UI_SETTING_TONE_HZ,
    UI_SETTING_KEY_IN_WPM,
    UI_SETTING_KEY_IN_MODE,
    UI_SETTING_KEY_OUT_MODE,
    UI_SETTING_KEYER_PADDLE_MODE,
    UI_SETTING_KEYER_TX_DELAY_S,
    UI_SETTING_KEYER_REPEAT_INTERVAL_S,
    UI_SETTING_KEYER_MESSAGE_1,
    UI_SETTING_KEYER_MESSAGE_2,
    UI_SETTING_KEYER_MESSAGE_3,
    UI_SETTING_KEYER_MESSAGE_4,
    UI_SETTING_KEYER_MESSAGE_5,
    UI_SETTING_KEYER_MUTE,
    UI_SETTING_KEYER_TUNE_TIMEOUT_S,
    UI_SETTING_KEYER_MYCALL,
    UI_SETTING_KEYER_SK_WPM,
} ui_setting_target_t;

#define UI_INPUT_EVENT_TEXT_MAX 127U

typedef struct {
    ui_input_event_type_t type;
    char key;
    ui_setting_target_t setting;
    int value;
    int delta;
    char text[UI_INPUT_EVENT_TEXT_MAX + 1U];
} ui_input_event_t;

typedef enum {
    UI_SERVICE_MODE_KEYER,
} ui_service_mode_t;

void ui_service_init(void);
void ui_service_show_demo_screen(void);
void ui_service_refresh(void);
ui_service_mode_t ui_service_get_mode(void);
ui_input_event_t ui_service_poll_input(void);
void ui_service_keyer_append_decoded_char(char ch);
void ui_service_keyer_backspace_decoded(void);
void ui_service_keyer_clear_decoded(void);
void ui_service_keyer_set_tx_text(const char *text);
void ui_service_keyer_set_status(const char *text);
bool ui_service_keyer_shortcut_active(void);
void ui_service_keyer_set_tune_active(bool active);
bool ui_service_keyer_tune_active(void);

#ifdef __cplusplus
}
#endif

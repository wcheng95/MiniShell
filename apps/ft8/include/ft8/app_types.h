#ifndef FT8_APP_TYPES_H
#define FT8_APP_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define UI_MAX_COLS 30
#define UI_MAX_ROWS 8
#define UI_MAIN_LINES 6
#define UI_TEXT_CAP (UI_MAX_COLS + 1)

/* One Ft8Engine window can return at most 50 decoded candidates. */
#define APP_MAX_RX_LINES 50
#define APP_MAX_TX_LINES UI_MAIN_LINES

typedef enum {
    SCREEN_RX = 0,
    SCREEN_TX,
    SCREEN_O,
    SCREEN_S,
    SCREEN_V
} Screen;

typedef enum {
    APP_ACTION_NONE = 0,
    APP_ACTION_SET_PROFILE,
    APP_ACTION_SET_BAND,
    APP_ACTION_SET_SKIP_TX1,
    APP_ACTION_SET_MAX_RETRY
} AppActionType;

typedef struct {
    AppActionType type;
    union {
        int index;
        int int_value;
        bool bool_value;
    } value;
} AppAction;

typedef struct {
    int profile_index;
    int profile_count;
    char profile_name[16];
    int band_index;
    int band_count;
    char band_name[8];
    bool skip_tx1;
    int max_retry;

    bool utc_valid;
    uint8_t utc_hour;
    uint8_t utc_minute;
    uint8_t utc_second;
    uint8_t slot_counter; /* 0..14, rendered as 0..E. */

    char rx_lines[APP_MAX_RX_LINES][UI_TEXT_CAP];
    size_t rx_count;
    char tx_lines[APP_MAX_TX_LINES][UI_TEXT_CAP];
    size_t tx_count;
} UiModel;

typedef struct {
    uint32_t column_count;
    uint32_t row_count;
    bool has_footer;
    char rows[UI_MAX_ROWS][UI_TEXT_CAP];
} UiFrame;

typedef enum {
    UI_INPUT_NONE = 0,
    UI_INPUT_CHAR,
    UI_INPUT_UP,
    UI_INPUT_DOWN,
    UI_INPUT_LEFT,
    UI_INPUT_RIGHT,
    UI_INPUT_ENTER,
    UI_INPUT_BACK,
    UI_INPUT_PAGE_PREV,
    UI_INPUT_PAGE_NEXT,
    UI_INPUT_QUIT
} UiInputType;

typedef struct {
    UiInputType type;
    int ch;
} UiInput;

#endif

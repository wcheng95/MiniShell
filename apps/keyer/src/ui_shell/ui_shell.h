#pragma once
#include "keyer_types.h"

#define UI_COLS 20u
#define UI_ROWS 7u
#define UI_HISTORY 1280u

typedef enum { UI_NONE, UI_CHAR, UI_OPT, UI_UP, UI_DOWN, UI_LEFT, UI_RIGHT,
               UI_ENTER, UI_BACKSPACE, UI_ESCAPE, UI_TAB, UI_ALT_KEY } ui_key_t;
#define UI_SHIFT 1u
#define UI_CTRL 2u
#define UI_ALT 4u
#define UI_FN 8u
#define UI_OPT_MOD 16u
typedef struct { ui_key_t key; uint32_t ch, mods; } ui_input_t;
typedef enum { UI_ACT_NONE, UI_ACT_QUIT, UI_ACT_SAVE, UI_ACT_TEXT,
               UI_ACT_MEMORY, UI_ACT_TUNE, UI_ACT_CANCEL, UI_ACT_START,
               UI_ACT_BACKSPACE } ui_action_t;
typedef struct { ui_action_t action; char ch; unsigned memory; } ui_result_t;
typedef struct {
    bool operation, editing, memory_overlay;
    unsigned page, selected;
    char edit[96];
    bool edit_fresh;
    keyer_config_t draft;
    char history[UI_HISTORY];
    unsigned history_len;
    char status[21];
    uint64_t status_until;
} ui_shell_t;
typedef struct { char rows[7][21]; int inverse_row; } ui_frame_t;

void ui_shell_init(ui_shell_t *ui);
ui_result_t ui_shell_input(ui_shell_t *ui, keyer_config_t *config, ui_input_t input);
void ui_shell_history(ui_shell_t *ui, char ch);
void ui_shell_status(ui_shell_t *ui, const char *text, uint64_t now);
void ui_shell_render(const ui_shell_t *ui, const keyer_config_t *config,
                     int utc_minutes, const char *tail, bool tune,
                     uint64_t now, ui_frame_t *frame);

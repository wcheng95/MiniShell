#ifndef FT8_UI_SHELL_H
#define FT8_UI_SHELL_H

#include <stdbool.h>

#include "ft8/app_types.h"
#include "presentation_profile.h"

typedef enum {
    UI_SUBMENU_NONE = 0,
    UI_SUBMENU_O_CQ,
    UI_SUBMENU_O_TX,
    UI_SUBMENU_O_MESSAGE,
    UI_SUBMENU_S_STATION,
    UI_SUBMENU_S_IO_PATHS,
    UI_SUBMENU_S_BAND_PROFILES,
    UI_SUBMENU_S_LOGGING,
    UI_SUBMENU_S_TIME_GPS,
    UI_SUBMENU_S_SYSTEM,
    UI_SUBMENU_V_STATUS,
    UI_SUBMENU_V_GPS,
    UI_SUBMENU_V_QSO,
    UI_SUBMENU_V_PERF,
    UI_SUBMENU_V_SYSTEM,
    UI_SUBMENU_V_ABOUT
} UiSubmenu;

typedef struct {
    Screen screen;
    UiSubmenu submenu;
    int selected_line;
    ft8_presentation_profile_t presentation;
} UiShell;

void ui_shell_init(UiShell *ui, ft8_presentation_profile_t presentation);
void ui_shell_render(const UiShell *ui, const UiModel *model, UiFrame *frame);
bool ui_shell_handle_input(UiShell *ui, const UiModel *model,
                           UiInput input, AppAction *action_out);

#endif

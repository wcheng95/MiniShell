#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "ui_shell.h"

static UiInput key(char c)
{
    UiInput input = {.type = UI_INPUT_CHAR, .ch = c};
    return input;
}

static void set_default_model(UiModel *model)
{
    memset(model, 0, sizeof(*model));
    model->profile_index = 0;
    model->profile_count = 2;
    snprintf(model->profile_name, sizeof(model->profile_name), "%s", "Default");
    model->band_index = 3;
    model->band_count = 7;
    snprintf(model->band_name, sizeof(model->band_name), "%s", "20m");
    model->max_retry = 3;
}

int main(void)
{
    UiShell ui;
    UiModel model;
    UiFrame frame;
    AppAction action;

    set_default_model(&model);
    ui_shell_init(&ui);
    ui_shell_render(&ui, &model, &frame);
    assert(strstr(frame.rows[0], "FT8") != NULL);
    assert(strstr(frame.rows[7], "R T O S V") != NULL);

    assert(!ui_shell_handle_input(&ui, &model, key('o'), &action));
    ui_shell_render(&ui, &model, &frame);
    assert(strstr(frame.rows[1], "Protocol: FT8") != NULL);
    assert(strstr(frame.rows[4], "CQ / Beacon") != NULL);
    assert(strstr(frame.rows[5], "TX >") != NULL);
    assert(strstr(frame.rows[6], "Message") != NULL);

    assert(!ui_shell_handle_input(&ui, &model, key('1'), &action));
    assert(action.type == APP_ACTION_NONE);

    assert(ui_shell_handle_input(&ui, &model, key('2'), &action));
    assert(action.type == APP_ACTION_SET_PROFILE);
    assert(action.value.index == 1);

    assert(!ui_shell_handle_input(&ui, &model, key('5'), &action));
    assert(ui.submenu == UI_SUBMENU_O_TX);
    ui_shell_render(&ui, &model, &frame);
    assert(strstr(frame.rows[3], "Skip TX1") != NULL);
    assert(strstr(frame.rows[4], "Max Retry") != NULL);

    assert(ui_shell_handle_input(&ui, &model, key('3'), &action));
    assert(action.type == APP_ACTION_SET_SKIP_TX1);
    assert(action.value.bool_value == true);

    assert(!ui_shell_handle_input(&ui, &model, key('s'), &action));
    ui_shell_render(&ui, &model, &frame);
    assert(strstr(frame.rows[1], "Station") != NULL);
    assert(strstr(frame.rows[2], "I/O Paths") != NULL);
    assert(strstr(frame.rows[3], "Band Profiles") != NULL);
    assert(strstr(frame.rows[6], "System") != NULL);

    assert(!ui_shell_handle_input(&ui, &model, key('2'), &action));
    assert(ui.submenu == UI_SUBMENU_S_IO_PATHS);
    ui_shell_render(&ui, &model, &frame);
    assert(strstr(frame.rows[1], "RX Audio") != NULL);
    assert(strstr(frame.rows[2], "TX Audio") != NULL);
    assert(strstr(frame.rows[3], "Control") != NULL);

    assert(!ui_shell_handle_input(&ui, &model, key('v'), &action));
    assert(!ui_shell_handle_input(&ui, &model, key('1'), &action));
    assert(ui.submenu == UI_SUBMENU_V_STATUS);
    ui_shell_render(&ui, &model, &frame);
    assert(strstr(frame.rows[1], "Protocol: FT8") != NULL);
    assert(strstr(frame.rows[4], "RX Audio") != NULL);
    assert(strstr(frame.rows[5], "TX Audio") != NULL);
    assert(strstr(frame.rows[6], "Control") != NULL);

    assert(!ui_shell_handle_input(&ui, &model, key('v'), &action));
    assert(!ui_shell_handle_input(&ui, &model, key('5'), &action));
    assert(ui.submenu == UI_SUBMENU_V_SYSTEM);
    ui_shell_render(&ui, &model, &frame);
    assert(strstr(frame.rows[1], "Runtime: MiniShell") != NULL);
    assert(strstr(frame.rows[3], "App: ft8") != NULL);

    puts("ft8_ui_smoke: PASS");
    return 0;
}

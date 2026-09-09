#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "presentation_profile.h"
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

static void test_profile_contract(void)
{
    ft8_presentation_profile_t profile;
    ft8_presentation_spec_t spec;

    assert(ft8_presentation_parse("desktop", &profile));
    assert(profile == FT8_PRESENTATION_DESKTOP);
    assert(strcmp(ft8_presentation_name(profile), "DESKTOP") == 0);
    assert(ft8_presentation_get_spec(profile, &spec));
    assert(spec.columns == 30u && spec.rows == 8u && spec.has_footer);

    assert(ft8_presentation_parse("ADV", &profile));
    assert(profile == FT8_PRESENTATION_ADV);
    assert(strcmp(ft8_presentation_name(profile), "ADV") == 0);
    assert(ft8_presentation_get_spec(profile, &spec));
    assert(spec.columns == 20u && spec.rows == 7u && !spec.has_footer);

    assert(!ft8_presentation_parse("linux", &profile));
}

static void test_desktop(void)
{
    UiShell ui;
    UiModel model;
    UiFrame frame;
    AppAction action;

    set_default_model(&model);
    ui_shell_init(&ui, FT8_PRESENTATION_DESKTOP);
    ui_shell_render(&ui, &model, &frame);
    assert(frame.column_count == 30u);
    assert(frame.row_count == 8u);
    assert(frame.has_footer);
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
    assert(strstr(frame.rows[2], "Presentation: DESKTOP") != NULL);
    assert(strstr(frame.rows[3], "UI: text 30x8") != NULL);
    assert(strstr(frame.rows[4], "App: ft8") != NULL);
}

static void test_adv(void)
{
    UiShell ui;
    UiModel model;
    UiFrame frame;
    AppAction action;

    set_default_model(&model);
    ui_shell_init(&ui, FT8_PRESENTATION_ADV);
    ui_shell_render(&ui, &model, &frame);
    assert(frame.column_count == 20u);
    assert(frame.row_count == 7u);
    assert(!frame.has_footer);
    assert(strstr(frame.rows[0], "FT8") != NULL);
    assert(strstr(frame.rows[0], "20m") != NULL);
    assert(strstr(frame.rows[0], "RX") != NULL);
    assert(frame.rows[7][0] == '\0');

    assert(!ui_shell_handle_input(&ui, &model, key('o'), &action));
    ui_shell_render(&ui, &model, &frame);
    assert(strstr(frame.rows[1], "Protocol: FT8") != NULL);
    assert(strstr(frame.rows[6], "Message") != NULL);

    assert(!ui_shell_handle_input(&ui, &model, key('v'), &action));
    assert(!ui_shell_handle_input(&ui, &model, key('5'), &action));
    ui_shell_render(&ui, &model, &frame);
    assert(strstr(frame.rows[1], "Runtime: MiniShell") != NULL);
    assert(strstr(frame.rows[2], "Presentation: ADV") != NULL);
    assert(strstr(frame.rows[3], "UI: text 20x7") != NULL);
    assert(strstr(frame.rows[4], "App: ft8") != NULL);
    assert(strstr(frame.rows[5], "Station: Default") != NULL);
    assert(strstr(frame.rows[6], "Band: 20m") != NULL);
}

int main(void)
{
    test_profile_contract();
    test_desktop();
    test_adv();
    puts("ft8_ui_smoke: PASS");
    return 0;
}

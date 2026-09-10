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

static UiInput special(UiInputType type)
{
    UiInput input = {.type = type, .ch = 0};
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
    model->utc_valid = true;
    model->utc_hour = 14u;
    model->utc_minute = 32u;
    model->utc_second = 8u;
    model->slot_counter = 8u;
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

static void test_desktop_existing_navigation(void)
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

    assert(!ui_shell_handle_input(&ui, &model, key('o'), &action));
    ui_shell_render(&ui, &model, &frame);
    assert(strstr(frame.rows[1], "Protocol: FT8") != NULL);
    assert(strstr(frame.rows[4], "CQ / Beacon") != NULL);

    assert(ui_shell_handle_input(&ui, &model, key('2'), &action));
    assert(action.type == APP_ACTION_SET_PROFILE);
    assert(action.value.index == 1);

    assert(!ui_shell_handle_input(&ui, &model, key('5'), &action));
    assert(ui.submenu == UI_SUBMENU_O_TX);
    assert(ui_shell_handle_input(&ui, &model, key('3'), &action));
    assert(action.type == APP_ACTION_SET_SKIP_TX1);
    assert(action.value.bool_value == true);
}

static void test_adv_locked_top_and_rx_paging(void)
{
    UiShell ui;
    UiModel model;
    UiFrame frame;
    AppAction action;
    size_t i;

    set_default_model(&model);
    {
        static const char *messages[] = {
            "RX message 1", "RX message 2", "RX message 3", "RX message 4",
            "RX message 5", "RX message 6", "RX message 7", "RX message 8"
        };
        model.rx_count = sizeof(messages) / sizeof(messages[0]);
        for (i = 0u; i < model.rx_count; ++i) {
            strcpy(model.rx_lines[i], messages[i]);
        }
    }

    ui_shell_init(&ui, FT8_PRESENTATION_ADV);
    ui_shell_render(&ui, &model, &frame);
    assert(frame.column_count == 20u);
    assert(frame.row_count == 7u);
    assert(!frame.has_footer);
    assert(strcmp(frame.rows[0], "RX 20 14:32:08 1/2 8") == 0);
    assert(strstr(frame.rows[1], "1 RX message 1") != NULL);
    assert(strstr(frame.rows[6], "6 RX message 6") != NULL);

    /* RX line keys emit absolute decoded-message indices, not page-local indices. */
    assert(ui_shell_handle_input(&ui, &model, key('6'), &action));
    assert(action.type == APP_ACTION_SELECT_RX_MESSAGE);
    assert(action.value.index == 5);

    assert(!ui_shell_handle_input(&ui, &model, special(UI_INPUT_DOWN), &action));
    ui_shell_render(&ui, &model, &frame);
    assert(strcmp(frame.rows[0], "RX 20 14:32:08 2/2 8") == 0);
    assert(strstr(frame.rows[1], "1 RX message 7") != NULL);
    assert(strstr(frame.rows[2], "2 RX message 8") != NULL);

    assert(ui_shell_handle_input(&ui, &model, key('2'), &action));
    assert(action.type == APP_ACTION_SELECT_RX_MESSAGE);
    assert(action.value.index == 7);
    assert(!ui_shell_handle_input(&ui, &model, key('3'), &action));
    assert(action.type == APP_ACTION_NONE);

    /* Page Down wraps 2/2 -> 1/2; Page Up wraps 1/2 -> 2/2. */
    assert(!ui_shell_handle_input(&ui, &model, special(UI_INPUT_PAGE_NEXT), &action));
    ui_shell_render(&ui, &model, &frame);
    assert(strcmp(frame.rows[0], "RX 20 14:32:08 1/2 8") == 0);

    assert(!ui_shell_handle_input(&ui, &model, special(UI_INPUT_UP), &action));
    ui_shell_render(&ui, &model, &frame);
    assert(strcmp(frame.rows[0], "RX 20 14:32:08 2/2 8") == 0);

    /* Screen switches always reset to the destination top level/page 1. */
    assert(!ui_shell_handle_input(&ui, &model, key('v'), &action));
    assert(ui.submenu == UI_SUBMENU_NONE);
    assert(ui.page_index == 0u);
    ui_shell_render(&ui, &model, &frame);
    assert(strcmp(frame.rows[0], "V  20 14:32:08 1/1 8") == 0);
    assert(strstr(frame.rows[1], "Memory") != NULL);
}

static void test_adv_tx_paging(void)
{
    UiShell ui;
    UiModel model;
    UiFrame frame;
    AppAction action;
    size_t i;

    set_default_model(&model);
    model.tx_count = 8u;
    for (i = 0u; i < model.tx_count; ++i) {
        snprintf(model.tx_lines[i], sizeof(model.tx_lines[i]),
                 "W%uXYZ    RPLY 0/3", (unsigned)(i + 1u));
    }

    ui_shell_init(&ui, FT8_PRESENTATION_ADV);
    assert(!ui_shell_handle_input(&ui, &model, key('t'), &action));
    ui_shell_render(&ui, &model, &frame);
    assert(strcmp(frame.rows[0], "TX 20 14:32:08 1/2 8") == 0);
    assert(strstr(frame.rows[1], "1 W1XYZ") != NULL);
    assert(strstr(frame.rows[6], "6 W6XYZ") != NULL);

    assert(!ui_shell_handle_input(&ui, &model, special(UI_INPUT_DOWN), &action));
    ui_shell_render(&ui, &model, &frame);
    assert(strcmp(frame.rows[0], "TX 20 14:32:08 2/2 8") == 0);
    assert(strstr(frame.rows[1], "1 W7XYZ") != NULL);
    assert(strstr(frame.rows[2], "2 W8XYZ") != NULL);

    assert(!ui_shell_handle_input(&ui, &model, special(UI_INPUT_PAGE_NEXT), &action));
    ui_shell_render(&ui, &model, &frame);
    assert(strcmp(frame.rows[0], "TX 20 14:32:08 1/2 8") == 0);
}

static void test_adv_memory_view(void)
{
    UiShell ui;
    UiModel model;
    UiFrame frame;
    AppAction action;

    set_default_model(&model);
    model.memory_app_valid = true;
    model.memory_app_allocated_bytes = 4u * 1024u;
    model.memory_app_allocation_count = 4u;
    model.memory_free_valid = true;
    model.memory_free_bytes = 200u * 1024u;
    model.memory_largest_valid = true;
    model.memory_largest_free_block = 180u * 1024u;
    model.rx_active = false;

    ui_shell_init(&ui, FT8_PRESENTATION_ADV);
    assert(!ui_shell_handle_input(&ui, &model, key('v'), &action));
    assert(!ui_shell_handle_input(&ui, &model, key('1'), &action));
    assert(ui.submenu == UI_SUBMENU_V_MEMORY);

    ui_shell_render(&ui, &model, &frame);
    assert(strcmp(frame.rows[0], "V  20 14:32:08 1/1 8") == 0);
    assert(strstr(frame.rows[1], "Heap free: 200.0K") != NULL);
    assert(strstr(frame.rows[2], "Largest: 180.0K") != NULL);
    assert(strstr(frame.rows[3], "App alloc: 4.0K") != NULL);
    assert(strstr(frame.rows[4], "Alloc count: 4") != NULL);
    assert(strstr(frame.rows[5], "Largest/free: 90%") != NULL);
    assert(strstr(frame.rows[6], "RX: OFF") != NULL);

    model.rx_active = true;
    ui_shell_render(&ui, &model, &frame);
    assert(strstr(frame.rows[6], "RX: ON") != NULL);

    model.memory_app_valid = false;
    model.memory_free_valid = false;
    model.memory_largest_valid = false;
    ui_shell_render(&ui, &model, &frame);
    assert(strstr(frame.rows[1], "Heap free: --") != NULL);
    assert(strstr(frame.rows[2], "Largest: --") != NULL);
    assert(strstr(frame.rows[3], "App alloc: --") != NULL);
    assert(strstr(frame.rows[4], "Alloc count: --") != NULL);
    assert(strstr(frame.rows[5], "Largest/free: --") != NULL);
}

static void test_adv_no_utc(void)
{
    UiShell ui;
    UiModel model;
    UiFrame frame;

    set_default_model(&model);
    model.utc_valid = false;
    model.slot_counter = 14u;
    ui_shell_init(&ui, FT8_PRESENTATION_ADV);
    ui_shell_render(&ui, &model, &frame);
    assert(strcmp(frame.rows[0], "RX 20 --:--:-- 1/1 E") == 0);
}

int main(void)
{
    test_profile_contract();
    test_desktop_existing_navigation();
    test_adv_locked_top_and_rx_paging();
    test_adv_tx_paging();
    test_adv_memory_view();
    test_adv_no_utc();
    puts("ft8_ui_smoke: PASS");
    return 0;
}

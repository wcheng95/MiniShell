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

    /* AS-5: line keys drop absolute QSO indices; Enter requests parity rotation. */
    assert(ui_shell_handle_input(&ui, &model, key('1'), &action));
    assert(action.type == APP_ACTION_DROP_TX_QSO);
    assert(action.value.index == 0);
    assert(ui_shell_handle_input(&ui, &model, special(UI_INPUT_ENTER), &action));
    assert(action.type == APP_ACTION_ROTATE_TX_QUEUE);

    assert(!ui_shell_handle_input(&ui, &model, special(UI_INPUT_DOWN), &action));
    ui_shell_render(&ui, &model, &frame);
    assert(strcmp(frame.rows[0], "TX 20 14:32:08 2/2 8") == 0);
    assert(strstr(frame.rows[1], "1 W7XYZ") != NULL);
    assert(strstr(frame.rows[2], "2 W8XYZ") != NULL);

    assert(ui_shell_handle_input(&ui, &model, key('2'), &action));
    assert(action.type == APP_ACTION_DROP_TX_QSO);
    assert(action.value.index == 7);
    assert(!ui_shell_handle_input(&ui, &model, key('3'), &action));
    assert(action.type == APP_ACTION_NONE);

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

static void test_cq_beacon_controls(void)
{
    UiShell ui; UiModel model; UiFrame frame; AppAction action;
    set_default_model(&model);
    ui_shell_init(&ui, FT8_PRESENTATION_ADV);
    assert(!ui_shell_handle_input(&ui,&model,key('O'),&action));
    assert(!ui_shell_handle_input(&ui,&model,key('4'),&action));
    assert(ui.submenu==UI_SUBMENU_O_CQ && ui.selected_line==0);
    const char *cq_names[]={"CQ Type: CQ", "CQ Type: CQ POTA"};
    const char *beacon_names[]={"Beacon: OFF", "Beacon: EVEN", "Beacon: ODD"};
    for (int cq=0;cq<2;++cq) for (int beacon=0;beacon<3;++beacon) {
        model.cq_type=(UiCqType)cq; model.beacon_mode=(UiBeaconMode)beacon;
        ui_shell_render(&ui,&model,&frame);
        assert(strstr(frame.rows[1],cq_names[cq]) && strstr(frame.rows[2],beacon_names[beacon]));
        assert(frame.column_count==20 && frame.row_count==7);
        for (unsigned row=0;row<frame.row_count;++row) assert(strlen(frame.rows[row])<=20);
    }
    for (int line=0;line<2;++line) {
        const UiInput inputs[]={key((char)('1'+line)),special(UI_INPUT_ENTER),
                                special(UI_INPUT_RIGHT),special(UI_INPUT_LEFT)};
        for (unsigned k=0;k<4;++k) for (int value=0;value<(line ? 3 : 2);++value) {
            model.cq_type=(UiCqType)value; model.beacon_mode=(UiBeaconMode)value;
            ui.selected_line=line;
            assert(ui_shell_handle_input(&ui,&model,inputs[k],&action));
            assert(action.type==(line ? APP_ACTION_SET_BEACON_MODE : APP_ACTION_SET_CQ_TYPE));
            assert(action.value.int_value==(line ? (value+(k==3 ? 2 : 1))%3 : 1-value));
            assert((int)model.cq_type==value && (int)model.beacon_mode==value);
        }
    }
    assert(!ui_shell_handle_input(&ui,&model,key('3'),&action) && action.type==APP_ACTION_NONE);
    assert(!ui_shell_handle_input(&ui,&model,special(UI_INPUT_BACK),&action));
    assert(ui.screen==SCREEN_O && ui.submenu==UI_SUBMENU_NONE);
    assert(!ui_shell_handle_input(&ui,&model,special(UI_INPUT_BACK),&action));
    assert(ui.screen==SCREEN_RX && ui.submenu==UI_SUBMENU_NONE);
}

static void test_qso_view(void)
{
    UiShell ui; UiModel model; UiFrame frame; AppAction action;
    set_default_model(&model); ui_shell_init(&ui, FT8_PRESENTATION_ADV);
    assert(!ui_shell_handle_input(&ui, &model, key('v'), &action));
    assert(ui_shell_handle_input(&ui, &model, key('3'), &action));
    assert(ui.submenu == UI_SUBMENU_V_QSO && action.type == APP_ACTION_LOAD_QSO_PAGE && action.value.page_index == 0);
    ui_shell_render(&ui, &model, &frame);
    assert(strstr(frame.rows[1], "No QSOs") && strstr(frame.rows[0], "1/1"));
    model.qso.status = QSO_VIEW_UTC_UNAVAILABLE;
    ui_shell_render(&ui, &model, &frame); assert(strstr(frame.rows[1], "UTC unavailable"));
    model.qso.status = QSO_VIEW_READ_ERROR;
    ui_shell_render(&ui, &model, &frame); assert(strstr(frame.rows[1], "QSO log read error"));
    model.qso.status = QSO_VIEW_OK; model.qso.total_count = 7; model.qso.page_count = 2;
    model.qso.row_count = 3;
    model.qso.rows[0] = (QsoSummary){.hour=3, .minute=4, .band="20m", .call="W6ABC"};
    model.qso.rows[1] = (QsoSummary){.hour=11, .minute=42, .band="17m", .call="W1AW/9"};
    model.qso.rows[2] = (QsoSummary){.hour=14, .minute=55, .band="40m", .call="VERYLONGCALL"};
    ui_shell_render(&ui, &model, &frame);
    assert(strstr(frame.rows[0], "1/2"));
    assert(strncmp(frame.rows[1], "03:04 20m W6ABC", 14) == 0);
    assert(strncmp(frame.rows[2], "11:42 17m W1AW/9", 15) == 0);
    assert(strcmp(frame.rows[3], "14:55 40m VERYLONGC>") == 0);
    for (unsigned i=0; i<frame.row_count; ++i) assert(strlen(frame.rows[i]) <= 20);
    const UiInputType nav[] = {UI_INPUT_DOWN, UI_INPUT_PAGE_NEXT, UI_INPUT_UP, UI_INPUT_PAGE_PREV};
    for (unsigned i=0; i<4; ++i) {
        assert(ui_shell_handle_input(&ui, &model, special(nav[i]), &action));
        assert(action.type == APP_ACTION_LOAD_QSO_PAGE && action.value.page_index == (i%2 ? 0u : 1u));
        model.qso.page_index = action.value.page_index;
        ui_shell_render(&ui, &model, &frame);
        assert(strstr(frame.rows[0], i%2 ? "1/2" : "2/2"));
    }
    UiShell before = ui;
    for (int i=1; i<=6; ++i) assert(!ui_shell_handle_input(&ui, &model, key('0'+i), &action));
    assert(!ui_shell_handle_input(&ui, &model, special(UI_INPUT_ENTER), &action));
    assert(!ui_shell_handle_input(&ui, &model, special(UI_INPUT_LEFT), &action));
    assert(!ui_shell_handle_input(&ui, &model, special(UI_INPUT_RIGHT), &action));
    assert(memcmp(&before, &ui, sizeof(ui)) == 0);
    model.qso.page_count = 12; model.qso.page_index = 11;
    ui_shell_render(&ui, &model, &frame); assert(strstr(frame.rows[0], "12/12"));
    assert(!ui_shell_handle_input(&ui, &model, special(UI_INPUT_BACK), &action));
    assert(ui.screen == SCREEN_V && ui.submenu == UI_SUBMENU_NONE);
    assert(ui_shell_handle_input(&ui, &model, key('3'), &action) && action.value.page_index == 0);
    assert(!ui_shell_handle_input(&ui, &model, key('r'), &action));
    assert(ui.screen == SCREEN_RX && ui.submenu == UI_SUBMENU_NONE);
}

int main(void)
{
    test_qso_view();
    test_cq_beacon_controls();
    test_profile_contract();
    test_desktop_existing_navigation();
    test_adv_locked_top_and_rx_paging();
    test_adv_tx_paging();
    test_adv_memory_view();
    test_adv_no_utc();
    puts("ft8_ui_smoke: PASS");
    return 0;
}

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
    model->cq_option_count = 5;
    strcpy(model->cq_text, "CQ");
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

static void test_rx_generation_pages(void)
{
    UiShell ui; UiModel model; UiFrame frame; AppAction action;
    set_default_model(&model);
    model.rx_count = model.tx_count = 18;
    model.rx_generation = 1;
    ui_shell_init(&ui, FT8_PRESENTATION_ADV);
    ui_shell_render(&ui, &model, &frame);
    assert(!ui_shell_handle_input(&ui, &model, key('.'), &action));
    ui.selected_line = 3;
    ui_shell_render(&ui, &model, &frame);
    assert(ui.page_index == 1 && ui.selected_line == 3 && strstr(frame.rows[0], "2/3"));
    ++model.rx_generation;
    ui_shell_render(&ui, &model, &frame);
    assert(ui.page_index == 0 && ui.selected_line == 0 && strstr(frame.rows[0], "1/3"));
    ui.page_index = 2;
    model.rx_count = 0; ++model.rx_generation;
    ui_shell_render(&ui, &model, &frame);
    assert(ui.page_index == 0 && strstr(frame.rows[0], "1/1"));
    assert(!ui_shell_handle_input(&ui, &model, key('t'), &action));
    ui.page_index = 2; ui.selected_line = 4;
    ++model.rx_generation;
    ui_shell_render(&ui, &model, &frame);
    assert(ui.page_index == 2 && ui.selected_line == 4 && strstr(frame.rows[0], "3/3"));
    assert(!ui_shell_handle_input(&ui, &model, key('r'), &action));
    assert(ui.page_index == 0 && ui.selected_line == 0);
    /* Input arriving before redraw must also use the new first page. */
    model.rx_count = 18; ui.page_index = 2; ++model.rx_generation;
    assert(ui_shell_handle_input(&ui, &model, key('1'), &action));
    assert(action.type == APP_ACTION_SELECT_RX_MESSAGE && action.value.index == 0);
}

static void test_cq_beacon_controls(void)
{
    UiShell ui; UiModel model; UiFrame frame; AppAction action;
    set_default_model(&model);
    ui_shell_init(&ui, FT8_PRESENTATION_ADV);
    assert(!ui_shell_handle_input(&ui,&model,key('O'),&action));
    assert(!ui_shell_handle_input(&ui,&model,key('4'),&action));
    assert(ui.submenu==UI_SUBMENU_O_CQ && ui.selected_line==0);
    const char *cq_names[]={"CQ", "CQ POTA", "CQ SOTA", "CQ DX", "CQ 250"};
    const char *beacon_names[]={"Beacon: OFF", "Beacon: EVEN", "Beacon: ODD"};
    for (int cq=0;cq<5;++cq) for (int beacon=0;beacon<3;++beacon) {
        model.cq_index=(unsigned)cq; strcpy(model.cq_text, cq_names[cq]); model.beacon_mode=(UiBeaconMode)beacon;
        ui_shell_render(&ui,&model,&frame);
        assert(strstr(frame.rows[1], "CQ Type: ") && strstr(frame.rows[1],cq_names[cq]) && strstr(frame.rows[2],beacon_names[beacon]));
        assert(frame.column_count==20 && frame.row_count==7);
        for (unsigned row=0;row<frame.row_count;++row) assert(strlen(frame.rows[row])<=20);
    }
    for (int line=0;line<2;++line) {
        const UiInput inputs[]={key((char)('1'+line)),special(UI_INPUT_ENTER),
                                special(UI_INPUT_RIGHT),special(UI_INPUT_LEFT)};
        for (unsigned k=0;k<4;++k) for (int value=0;value<(line ? 3 : 5);++value) {
            model.cq_index=(unsigned)value; model.beacon_mode=(UiBeaconMode)value;
            ui.selected_line=line;
            assert(ui_shell_handle_input(&ui,&model,inputs[k],&action));
            assert(action.type==(line ? APP_ACTION_SET_BEACON_MODE : APP_ACTION_SET_CQ_TYPE));
            assert(action.value.int_value==(line ? (value+(k==3 ? 2 : 1))%3 : (value+(k==3 ? 4 : 1))%5));
            assert((int)model.cq_index==value && (int)model.beacon_mode==value);
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
    ui_shell_render(&ui, &model, &frame);
    assert(strcmp(frame.rows[0], "V  20 14:32 12/12 8 ") == 0);
    model.qso.page_count = 100; model.qso.page_index = 99;
    ui_shell_render(&ui, &model, &frame);
    assert(strcmp(frame.rows[0], "V  20 14:32 100+ 8  ") == 0);
    model.qso.page_count = 102; model.qso.page_index = 100;
    ui_shell_render(&ui, &model, &frame);
    assert(strcmp(frame.rows[0], "V  20 14:32 100+ 8  ") == 0);
    assert(!ui_shell_handle_input(&ui, &model, special(UI_INPUT_BACK), &action));
    assert(ui.screen == SCREEN_V && ui.submenu == UI_SUBMENU_NONE);
    assert(ui_shell_handle_input(&ui, &model, key('3'), &action) && action.value.page_index == 0);
    assert(!ui_shell_handle_input(&ui, &model, key('r'), &action));
    assert(ui.screen == SCREEN_RX && ui.submenu == UI_SUBMENU_NONE);
}

static void color_status(void)
{
    UiShell ui; UiModel model; UiFrame frame, before;
    set_default_model(&model); ui_shell_init(&ui,FT8_PRESENTATION_ADV);
    model.rx_count=8;
    for (unsigned i=0;i<8;++i) {
        snprintf(model.rx_lines[i],UI_TEXT_CAP,"message %u",i);
        model.rx_kind[i]=i%3==0?UI_RX_TO_ME:i%3==1?UI_RX_CQ:UI_RX_NORMAL;
    }
    ui_shell_render(&ui,&model,&before);
    assert(before.separator_after_top && before.separator_color==UI_COLOR_WHITE);
    for(unsigned page=0;page<2;++page) {
        ui.page_index=page; ui_shell_render(&ui,&model,&frame);
        for(unsigned row=1;row<7;++row) {
            unsigned index=page*6+row-1;
            UiColor expected=index>=8?UI_COLOR_WHITE:index%3==0?UI_COLOR_RED:index%3==1?UI_COLOR_GREEN:UI_COLOR_WHITE;
            assert(frame.row_color[row]==expected);
            if(index<8){char expected_text[32];snprintf(expected_text,sizeof(expected_text),"%u message %u",row,index);
                assert(!strncmp(frame.rows[row],expected_text,strlen(expected_text)));}
        }
    }
    ui.page_index=0; model.tx_active=true; ui_shell_render(&ui,&model,&frame);
    assert(frame.separator_color==UI_COLOR_RED && frame.row_color[0]==UI_COLOR_WHITE);
    assert(!memcmp(before.rows,frame.rows,sizeof(frame.rows)) && !memcmp(before.row_color,frame.row_color,sizeof(frame.row_color)));
    for(Screen screen=SCREEN_TX;screen<=SCREEN_V;screen++) {
        ui.screen=screen; ui_shell_render(&ui,&model,&frame);
        assert(frame.separator_after_top && frame.separator_color==UI_COLOR_RED);
        for(unsigned r=0;r<UI_MAX_ROWS;++r) assert(frame.row_color[r]==UI_COLOR_WHITE);
    }
}

static void test_plain_page_shortcuts(void)
{
    UiModel model;
    AppAction action;
    set_default_model(&model);
    model.rx_count = model.tx_count = 13;
    model.tx_active = true;
    for (unsigned i = 0; i < 13; ++i) {
        snprintf(model.rx_lines[i], UI_TEXT_CAP, "RX %u", i);
        snprintf(model.tx_lines[i], UI_TEXT_CAP, "TX %u", i);
        model.rx_kind[i] = i % 3 == 0 ? UI_RX_TO_ME :
                           i % 3 == 1 ? UI_RX_CQ : UI_RX_NORMAL;
    }
    const UiInputType nav[] = {UI_INPUT_UP, UI_INPUT_PAGE_PREV,
                              UI_INPUT_DOWN, UI_INPUT_PAGE_NEXT};
    const ft8_presentation_profile_t profiles[] = {
        FT8_PRESENTATION_ADV, FT8_PRESENTATION_DESKTOP
    };
    for (unsigned profile = 0; profile < 2; ++profile) {
        for (Screen screen = SCREEN_RX; screen <= SCREEN_TX; ++screen) {
            UiShell ui;
            ui_shell_init(&ui, profiles[profile]);
            ui.screen = screen;
            /* Every page and direction, including both wrap boundaries. */
            for (unsigned page = 0; page < 3; ++page) {
                for (unsigned n = 0; n < 4; ++n) {
                    ui.page_index = page;
                    ui.selected_line = 4;
                    UiShell expected = ui;
                    UiFrame plain_frame, special_frame;
                    assert(!ui_shell_handle_input(&expected, &model, special(nav[n]), &action));
                    action.type = APP_ACTION_LOAD_QSO_PAGE;
                    assert(!ui_shell_handle_input(&ui, &model, key(n < 2 ? ';' : '.'), &action));
                    assert(action.type == APP_ACTION_NONE);
                    assert(ui.page_index == (page + (n < 2 ? 2 : 1)) % 3);
                    assert(ui.selected_line == 0);
                    assert(ui.page_index == expected.page_index);
                    ui_shell_render(&ui, &model, &plain_frame);
                    ui_shell_render(&expected, &model, &special_frame);
                    assert(!memcmp(&plain_frame, &special_frame, sizeof(plain_frame)));
                    if (profiles[profile] == FT8_PRESENTATION_DESKTOP)
                        assert(strstr(plain_frame.rows[7], ";/. page"));
                }
            }
            ui.page_index = 1;
            for (char c = '1'; c <= '6'; ++c) {
                assert(ui_shell_handle_input(&ui, &model, key(c), &action));
                assert(action.type == (screen == SCREEN_RX ? APP_ACTION_SELECT_RX_MESSAGE : APP_ACTION_DROP_TX_QSO));
                assert(action.value.index == 6 + c - '1');
            }
            model.rx_count = model.tx_count = 6;
            ui.page_index = 0;
            ui.selected_line = 4;
            for (unsigned n = 0; n < 2; ++n) {
                assert(!ui_shell_handle_input(&ui, &model, key(n ? '.' : ';'), &action));
                assert(action.type == APP_ACTION_NONE && ui.page_index == 0);
                assert(ui.selected_line == 4); /* Existing single-page move_page semantics. */
            }
            model.rx_count = model.tx_count = 13;
        }
    }
    /* Neither other screens nor any submenu consumes characters as paging. */
    for (Screen screen = SCREEN_RX; screen <= SCREEN_V; ++screen) {
        for (UiSubmenu submenu = UI_SUBMENU_NONE; submenu <= UI_SUBMENU_V_ABOUT; ++submenu) {
            if (screen <= SCREEN_TX && submenu == UI_SUBMENU_NONE) continue;
            UiShell ui;
            ui_shell_init(&ui, FT8_PRESENTATION_ADV);
            ui.screen = screen; ui.submenu = submenu;
            ui.page_index = 1; ui.selected_line = 4;
            UiShell before = ui;
            for (unsigned n = 0; n < 2; ++n) {
                assert(!ui_shell_handle_input(&ui, &model, key(n ? '.' : ';'), &action));
                assert(action.type == APP_ACTION_NONE);
                assert(!memcmp(&ui, &before, sizeof(ui)));
            }
        }
    }
    UiShell ui;
    ui_shell_init(&ui, FT8_PRESENTATION_ADV);
    const char switches[] = "rtosv";
    for (unsigned i = 0; i < 5; ++i) {
        assert(!ui_shell_handle_input(&ui, &model, key(switches[i]), &action));
        assert(ui.screen == (Screen)i && ui.submenu == UI_SUBMENU_NONE);
    }
}

static void test_about_version(void)
{
    const ft8_presentation_profile_t profiles[] = {
        FT8_PRESENTATION_DESKTOP, FT8_PRESENTATION_ADV
    };
    for (unsigned i = 0; i < sizeof(profiles) / sizeof(profiles[0]); ++i) {
        UiShell ui;
        UiModel model;
        UiFrame frame;
        AppAction action;
        set_default_model(&model);
        ui_shell_init(&ui, profiles[i]);
        assert(!ui_shell_handle_input(&ui, &model, key('V'), &action));
        assert(!ui_shell_handle_input(&ui, &model, key('6'), &action));
        assert(ui.submenu == UI_SUBMENU_V_ABOUT);
        ui_shell_render(&ui, &model, &frame);
        char expected[sizeof(frame.rows[1])];
        snprintf(expected, sizeof(expected), "%-*s", (int)frame.column_count,
                 "MiniFT8-V3.083");
        assert(strcmp(frame.rows[1], expected) == 0);
    }
}

int main(void)
{
    test_about_version();
    test_plain_page_shortcuts();
    color_status();
    test_qso_view();
    test_cq_beacon_controls();
    test_rx_generation_pages();
    test_profile_contract();
    test_desktop_existing_navigation();
    test_adv_locked_top_and_rx_paging();
    test_adv_tx_paging();
    test_adv_memory_view();
    test_adv_no_utc();
    puts("ft8_ui_smoke: PASS");
    return 0;
}

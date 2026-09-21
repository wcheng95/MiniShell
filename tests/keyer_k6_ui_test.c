#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ui_shell.h"
#include "config_service.h"
static ui_shell_t u;
static keyer_config_t c;
static ui_result_t input(ui_key_t k, unsigned ch, unsigned mods)
{ return ui_shell_input(&u, &c, (ui_input_t){k,ch,mods}); }
static void reset(void) { ui_shell_init(&u); config_service_defaults(&c); }
static void render_tests(void)
{
    reset(); ui_frame_t f;
    const char *kin[] = {"Pdl", "PdR", "SkT", "SkR"};
    const char *kout[] = {"SKS", "SKM", "OFF"};
    for (unsigned i = 0; i < 4; ++i) for (unsigned j = 0; j < 3; ++j) {
        c.key_in_mode = (keyer_key_in_mode_t)i; c.key_out_mode = (keyer_key_out_mode_t)j;
        ui_shell_render(&u, &c, 1173, "", false, 0, &f);
        char expected[21]; snprintf(expected, sizeof(expected), "19:33 %s %s 20 V80", kin[i], kout[j]);
        assert(strlen(f.rows[0]) == 20 && !strcmp(f.rows[0], expected));
    }
    reset(); ui_shell_render(&u, &c, -1, "123456789012345678901", false, 0, &f);
    assert(!strcmp(f.rows[0], "--:-- Pdl SKS 20 V80"));
    assert(!strcmp(f.rows[6], "23456789012345678901"));
    ui_shell_status(&u, "Saved", 0); ui_shell_render(&u, &c, 0, "Q", false, 1199999, &f);
    assert(!strncmp(f.rows[6], "Saved", 5));
    ui_shell_render(&u, &c, 0, "Q", false, 1200000, &f); assert(f.rows[6][0] == 'Q');
    for (unsigned i = 0; i < 1300; ++i) ui_shell_history(&u, (char)('A' + (i / 20) % 26));
    assert(u.history_len == 1280); ui_shell_render(&u, &c, 0, "", false, 0, &f);
    for (unsigned r = 0; r < 5; ++r) for (unsigned col = 0; col < 20; ++col)
        assert(f.rows[r+1][col] == (char)('A' + (60+r)%26));
    ui_shell_history(&u, '\b'); assert(u.history_len == 1279);
    ui_shell_history(&u, '\n'); assert(u.history_len == 1280);
    ui_shell_history(&u, '~'); assert(u.history_len == 1261 && u.history[1260] == '~');
}
static void inputs(void)
{
    reset();
    for (const char *s = "cCQOqo0123456789"; *s; ++s) assert(input(UI_CHAR, *s, 0).action == UI_ACT_TEXT);
    assert(input(UI_CHAR, 'c', UI_CTRL).action == UI_ACT_QUIT);
    assert(input(UI_CHAR, 'C', UI_CTRL | UI_SHIFT).action == UI_ACT_QUIT);
    assert(input(UI_CHAR, 'c', UI_ALT).action == UI_ACT_NONE);
    assert(input(UI_CHAR, '[', UI_CTRL).action == UI_ACT_NONE && c.wpm == 20);
    assert(input(UI_CHAR, '[', 0).action == UI_ACT_SAVE && c.wpm == 19);
    assert(input(UI_CHAR, ']', 0).action == UI_ACT_SAVE && c.wpm == 20);
    for (unsigned i=0;i<100;++i) input(UI_CHAR, '[', 0);
    assert(c.wpm == 5); for (unsigned i=0;i<100;++i) input(UI_CHAR, ']', 0); assert(c.wpm == 60);
    assert(input(UI_CHAR, '{', UI_SHIFT).action == UI_ACT_SAVE && c.volume == 75);
    for (unsigned i=0;i<30;++i) input(UI_CHAR, '}', UI_SHIFT);
    assert(c.volume == 99);
    for (unsigned i=0;i<30;++i) input(UI_CHAR, '{', UI_SHIFT);
    assert(c.volume == 0);
    for (unsigned i=0;i<5;++i) { ui_result_t r=input(UI_CHAR, '1'+i, UI_ALT); assert(r.action==UI_ACT_MEMORY && r.memory==i); }
    assert(input(UI_CHAR,'1',UI_ALT|UI_CTRL).action==UI_ACT_NONE);
    assert(input(UI_CHAR,'`',0).action==UI_ACT_CANCEL);
    assert(input(UI_CHAR,'`',UI_FN).action==UI_ACT_NONE);
    assert(input(UI_ENTER,0,0).action==UI_ACT_START);
    assert(input(UI_BACKSPACE,0,0).action==UI_ACT_BACKSPACE);
    assert(input(UI_TAB,0,0).action==UI_ACT_TUNE);
}
static void editors(void)
{
    reset(); input(UI_OPT,0,0); assert(u.operation);
    input(UI_UP,0,UI_FN); assert(u.page==2); input(UI_DOWN,0,UI_FN); assert(u.page==0);
    input(UI_DOWN,0,UI_FN); assert(u.page==1); input(UI_DOWN,0,UI_FN); assert(u.page==2);
    input(UI_DOWN,0,UI_FN); assert(u.page==0);
    input(UI_CHAR,'3',0); assert(u.editing);
    input(UI_CHAR,'4',0); input(UI_CHAR,'2',0);
    assert(input(UI_ENTER,0,0).action==UI_ACT_SAVE && c.wpm==42);
    input(UI_CHAR,'3',0); input(UI_CHAR,'9',0); input(UI_ESCAPE,0,UI_FN); assert(!u.editing && c.wpm==42);
    input(UI_CHAR,'1',0); input(UI_RIGHT,0,0); input(UI_ENTER,0,0); assert(c.key_in_mode==KEYER_KEY_IN_PADDLE_R);
    input(UI_DOWN,0,UI_FN); input(UI_CHAR,'2',0);
    for(unsigned i=0;i<100;++i) input(UI_CHAR,'a',0);
    assert(strlen(u.edit)==95); input(UI_BACKSPACE,0,0); assert(strlen(u.edit)==94);
    input(UI_CHAR,'=',0); assert(input(UI_ENTER,0,0).action==UI_ACT_SAVE && strlen(c.messages[1])==95);
    input(UI_CHAR,'2',0); input(UI_BACKSPACE,0,0); input(UI_OPT,0,0);
    assert(!u.operation && !u.editing && strlen(c.messages[1])==95);
    /* Every exposed setting can enter/commit; reserved rows cannot edit. */
    for(unsigned page=0;page<3;++page) for(unsigned row=0;row<6;++row) {
        reset(); u.operation=true; u.page=page; input(UI_CHAR,'1'+row,0);
        if(page==2 && row>=3) {assert(!u.editing);continue;}
        assert(u.editing); assert(input(UI_ENTER,0,0).action==UI_ACT_SAVE);
    }
}
int main(void) { render_tests(); inputs(); editors(); puts("keyer K6 UI: PASS"); }

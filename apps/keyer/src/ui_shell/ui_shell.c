#include "ui_shell.h"
#include "keyer_text.h"

static const char *const kin[] = {"Pdl", "PdR", "SkT", "SkR"};
static const char *const kout[] = {"SKS", "SKM", "OFF"};
static const char *const paddle[] = {"IambicA", "IambicB", "Bug"};
static unsigned clamp(int n, unsigned low, unsigned high)
{ return n < (int)low ? low : n > (int)high ? high : (unsigned)n; }
static unsigned item(const ui_shell_t *u) { return u->page * 6u + u->selected; }
static unsigned value(const keyer_config_t *c, unsigned i)
{
    switch (i) {
    case 0: return c->key_in_mode; case 1: return c->key_out_mode;
    case 2: return c->wpm; case 3: return c->volume;
    case 4: return c->sidetone_hz; case 5: return c->paddle_mode;
    case 11: return c->repeat_s; case 12: return c->tx_delay_s;
    case 13: return c->tune_timeout_s; case 14: return c->mute;
    default: return 0;
    }
}
static unsigned minimum(unsigned i) { return i == 2 ? 5 : i == 4 ? 300 : i == 11 ? 1 : 0; }
static unsigned maximum(unsigned i)
{
    switch (i) {
    case 0: return 3; case 1: case 5: return 2; case 2: return 60;
    case 4: return 999; case 13: return 20; case 14: return 1;
    default: return 99;
    }
}
static void set_value(keyer_config_t *c, unsigned i, unsigned v)
{
    switch (i) {
    case 0: c->key_in_mode = (keyer_key_in_mode_t)v; break;
    case 1: c->key_out_mode = (keyer_key_out_mode_t)v; break;
    case 2: c->wpm = (uint8_t)v; break;
    case 3: c->volume = (uint8_t)v; break;
    case 4: c->sidetone_hz = (uint16_t)v; break;
    case 5: c->paddle_mode = (keyer_engine_paddle_mode_t)v; break;
    case 11: c->repeat_s = (uint8_t)v; break;
    case 12: c->tx_delay_s = (uint8_t)v; break;
    case 13: c->tune_timeout_s = (uint8_t)v; break;
    case 14: c->mute = v != 0; break;
    default: break;
    }
}
static bool message(unsigned i) { return i >= 6 && i <= 10; }
static bool choice(unsigned i) { return i == 0 || i == 1 || i == 5 || i == 14; }
void ui_shell_init(ui_shell_t *u)
{
    u->operation = u->editing = false; u->page = u->selected = 0;
    u->history_len = 0; u->status[0] = 0; u->status_until = 0;
    u->edit[0] = 0; u->edit_fresh = true;
}
void ui_shell_status(ui_shell_t *u, const char *s, uint64_t now)
{
    keyer_text_copy(u->status, sizeof(u->status), s); u->status_until = now + 1200000u;
}
void ui_shell_history(ui_shell_t *u, char ch)
{
    if (ch == '\b') { if (u->history_len) --u->history_len; return; }
    if (ch == '\n') {
        unsigned n = 20u - u->history_len % 20u;
        while (n--) ui_shell_history(u, ' ');
        return;
    }
    if (ch < 32 || ch > 126) return;
    if (u->history_len == UI_HISTORY) {
        for (unsigned i = 20; i < UI_HISTORY; ++i) u->history[i - 20] = u->history[i];
        u->history_len -= 20;
    }
    u->history[u->history_len++] = ch;
}
static void begin_edit(ui_shell_t *u, const keyer_config_t *c)
{
    unsigned i = item(u);
    if (i > 14) return;
    u->draft = *c; u->editing = true; u->edit_fresh = true;
    if (message(i)) keyer_text_copy(u->edit, sizeof(u->edit), c->messages[i - 6]);
    else keyer_text_number(u->edit, value(c, i));
}
ui_result_t ui_shell_input(ui_shell_t *u, keyer_config_t *c, ui_input_t e)
{
    ui_result_t r = {UI_ACT_NONE, 0, 0};
    if (e.key == UI_CHAR && (e.ch == 'c' || e.ch == 'C') &&
        (e.mods & UI_CTRL) && !(e.mods & (UI_ALT | UI_FN | UI_OPT_MOD))) {
        r.action = UI_ACT_QUIT; return r;
    }
    if (e.key == UI_OPT) {
        u->editing = false; u->operation = !u->operation; return r;
    }
    if (u->operation) {
        unsigned i = item(u);
        if (e.key == UI_ESCAPE || (e.key == UI_CHAR && e.ch == '`' && !e.mods)) {
            if (u->editing) u->editing = false; else u->operation = false;
            return r;
        }
        if (!u->editing && e.mods == UI_FN && (e.key == UI_UP || e.key == UI_DOWN)) {
            u->page = (u->page + (e.key == UI_UP ? 2 : 1)) % 3;
            u->selected = 0; return r;
        }
        /* ADV arrows carry Fn: inside an editor they adjust the value,
         * while the top-level page navigation above retains ownership. */
        if (u->editing && e.mods == UI_FN &&
            (e.key == UI_LEFT || e.key == UI_RIGHT || e.key == UI_UP || e.key == UI_DOWN))
            e.mods = 0;
        if (e.mods & (UI_CTRL | UI_ALT | UI_FN | UI_OPT_MOD)) return r;
        if (!u->editing) {
            if (e.key == UI_UP) u->selected = (u->selected + 5) % 6;
            if (e.key == UI_DOWN) u->selected = (u->selected + 1) % 6;
            if (e.key == UI_CHAR && e.ch >= '1' && e.ch <= '6') {
                u->selected = e.ch - '1'; begin_edit(u, c);
            } else if (e.key == UI_ENTER) begin_edit(u, c);
            return r;
        }
        if (e.key == UI_ENTER) {
            if (message(i)) keyer_text_copy(u->draft.messages[i - 6], 96, u->edit);
            else if (!choice(i)) {
                unsigned n = 0;
                if (!u->edit[0]) return r;
                for (unsigned j = 0; u->edit[j]; ++j) n = n * 10u + (unsigned)(u->edit[j] - '0');
                if (n < minimum(i) || n > maximum(i)) return r;
                set_value(&u->draft, i, n);
            }
            *c = u->draft; u->editing = false; r.action = UI_ACT_SAVE; return r;
        }
        if (!message(i) && (e.key == UI_LEFT || e.key == UI_RIGHT || e.key == UI_UP || e.key == UI_DOWN)) {
            int delta = e.key == UI_RIGHT || e.key == UI_UP ? 1 : -1;
            unsigned v = value(&u->draft, i);
            if (!choice(i) && u->edit[0]) {
                v = 0; for (unsigned j = 0; u->edit[j]; ++j) v = v * 10 + (unsigned)(u->edit[j] - '0');
            }
            v = clamp((int)v + delta, minimum(i), maximum(i));
            set_value(&u->draft, i, v); keyer_text_number(u->edit, v); return r;
        }
        if (choice(i)) return r;
        size_t n = keyer_text_len(u->edit);
        if (e.key == UI_BACKSPACE) { if (n) u->edit[n - 1] = 0; u->edit_fresh = false; }
        if (e.key == UI_CHAR && e.ch >= 32 && e.ch <= 126 &&
            (message(i) || (e.ch >= '0' && e.ch <= '9'))) {
            if (!message(i) && u->edit_fresh) n = 0;
            unsigned limit = message(i) ? 95 : 3;
            if (n < limit) { u->edit[n++] = (char)e.ch; u->edit[n] = 0; }
            u->edit_fresh = false;
        }
        return r;
    }
    if (e.mods == UI_ALT && e.key == UI_CHAR && e.ch >= '1' && e.ch <= '5') {
        r.action = UI_ACT_MEMORY; r.memory = e.ch - '1'; return r;
    }
    if (e.mods & (UI_CTRL | UI_ALT | UI_FN | UI_OPT_MOD)) return r;
    switch (e.key) {
    case UI_TAB: r.action = UI_ACT_TUNE; break;
    case UI_ENTER: r.action = UI_ACT_START; break;
    case UI_BACKSPACE: r.action = UI_ACT_BACKSPACE; break;
    case UI_CHAR:
        if (e.ch == '`' && !e.mods) r.action = UI_ACT_CANCEL;
        else if (e.ch == '\\' && !e.mods) {
            c->mute = !c->mute; r.action = UI_ACT_SAVE;
        } else if ((e.ch == '[' || e.ch == ']') && !e.mods) {
            c->wpm = (uint8_t)clamp(c->wpm + (e.ch == ']' ? 1 : -1), 5, 60); r.action = UI_ACT_SAVE;
        } else if (e.ch == '{' || e.ch == '}') {
            c->volume = (uint8_t)clamp(c->volume + (e.ch == '}' ? 5 : -5), 0, 99); r.action = UI_ACT_SAVE;
        } else if (e.ch >= 32 && e.ch <= 126) { r.action = UI_ACT_TEXT; r.ch = (char)e.ch; }
        break;
    default: break;
    }
    return r;
}
static void put(char *row, unsigned column, const char *s)
{ while (*s && column < 20) row[column++] = *s++; }
static void two(char *p, unsigned n) { p[0] = (char)('0' + n / 10); p[1] = (char)('0' + n % 10); }
void ui_shell_render(const ui_shell_t *u, const keyer_config_t *c, int minutes,
                     const char *tail, bool tune, uint64_t now, ui_frame_t *f)
{
    for (unsigned r = 0; r < 7; ++r) {
        for (unsigned j = 0; j < 20; ++j) f->rows[r][j] = ' ';
        f->rows[r][20] = 0;
    }
    f->inverse_row = -1;
    if (!u->operation) {
        char *h = f->rows[0];
        if (minutes < 0) put(h, 0, "--:--");
        else { two(h, (unsigned)minutes / 60 % 24); h[2] = ':'; two(h + 3, (unsigned)minutes % 60); }
        put(h, 6, kin[c->key_in_mode]); put(h, 10, kout[c->key_out_mode]);
        two(h + 14, c->wpm); h[17] = 'V'; two(h + 18, c->volume);
        unsigned lines = (u->history_len + 19) / 20;
        unsigned start = lines > 5 ? (lines - 5) * 20 : 0;
        for (unsigned i = start; i < u->history_len; ++i) f->rows[1 + (i - start) / 20][(i - start) % 20] = u->history[i];
        if (u->status[0] && now < u->status_until) put(f->rows[6], 0, u->status);
        else if (tune) put(f->rows[6], 0, "Tune:Hold");
        else { size_t n = keyer_text_len(tail); put(f->rows[6], 0, tail + (n > 20 ? n - 20 : 0)); }
        return;
    }
    put(f->rows[0], 0, "Operation O1"); f->rows[0][11] = (char)('1' + u->page);
    static const char *const labels[] = {"KeyIn: ", "KeyOut: ", "Speed: ", "Volume: ", "Tone: ", "Paddle: ",
        "M1: ", "M2: ", "M3: ", "M4: ", "M5: ", "Repeat: ", "TxDelay: ", "TuneTimeout: ", "Mute: "};
    for (unsigned r = 0; r < 6; ++r) {
        unsigned i = u->page * 6 + r;
        if (i > 14) continue;
        char *line = f->rows[r + 1]; line[0] = (char)('1' + r);
        put(line, 2, labels[i]);
        const keyer_config_t *v = u->editing && r == u->selected ? &u->draft : c;
        char numeric[11]; const char *text;
        if (i == 0) text = kin[v->key_in_mode];
        else if (i == 1) text = kout[v->key_out_mode];
        else if (i == 5) text = paddle[v->paddle_mode];
        else if (i == 14) text = v->mute ? "ON" : "OFF";
        else if (u->editing && r == u->selected) text = u->edit;
        else if (message(i)) text = v->messages[i - 6];
        else { keyer_text_number(numeric, value(v, i)); text = numeric; }
        unsigned col = 2 + (unsigned)keyer_text_len(labels[i]);
        size_t n = keyer_text_len(text);
        if (u->editing && r == u->selected && n > 20 - col) text += n - (20 - col);
        put(line, col, text);
    }
    f->inverse_row = (int)u->selected + 1;
    if (u->editing) put(f->rows[0], 13, "Edit");
    if (u->status[0] && now < u->status_until) {
        for (unsigned j = 0; j < 20; ++j) f->rows[0][j] = ' ';
        put(f->rows[0], 0, u->status);
    }
}

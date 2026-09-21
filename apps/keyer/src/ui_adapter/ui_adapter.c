#include "ui_adapter.h"
#include <stddef.h>
mini_result_t ui_adapter_open(ui_adapter_t *a, const mini_api_t *api)
{
    a->display = NULL; a->input = NULL; a->presented = false;
    if (!api || !api->display || !api->display->text ||
        !api->display->text->get_info || !api->display->text->write_at_attr ||
        !api->display->text->clear || !api->display->present ||
        !api->input || !api->input->key || !api->input->key->read) return MINI_ERR_NOT_READY;
    mini_text_display_info_t info = {.struct_size = sizeof(info)};
    mini_result_t rc = api->display->text->get_info(&info);
    if (rc != MINI_OK) return rc;
    if (info.columns < 20 || info.rows < 7) return MINI_ERR_UNSUPPORTED;
    a->display = api->display; a->input = api->input->key;
    return a->display->text->clear();
}
bool ui_adapter_read(ui_adapter_t *a, ui_input_t *out)
{
    mini_key_event_t e = {.struct_size = sizeof(e)};
    if (a->input->read(&e, MINI_WAIT_NONE) != MINI_OK) return false;
    out->key = UI_NONE; out->ch = 0; out->mods = 0;
    if (e.modifiers & MINI_MOD_CTRL) out->mods |= UI_CTRL;
    if (e.modifiers & MINI_MOD_SHIFT) out->mods |= UI_SHIFT;
    if (e.modifiers & MINI_MOD_ALT) out->mods |= UI_ALT;
    if (e.modifiers & MINI_MOD_FN) out->mods |= UI_FN;
    if (e.modifiers & MINI_MOD_OPT) out->mods |= UI_OPT_MOD;
    if (e.type == MINI_KEY_EVENT_CHAR) { out->key = UI_CHAR; out->ch = e.codepoint; }
    else if (e.type == MINI_KEY_EVENT_SPECIAL) {
        switch (e.key) {
        case MINI_KEY_ALT: out->key = UI_ALT_KEY; break;
        case MINI_KEY_OPT: out->key = UI_OPT; break;
        case MINI_KEY_UP: out->key = UI_UP; break;
        case MINI_KEY_DOWN: out->key = UI_DOWN; break;
        case MINI_KEY_LEFT: out->key = UI_LEFT; break;
        case MINI_KEY_RIGHT: out->key = UI_RIGHT; break;
        case MINI_KEY_ENTER: out->key = UI_ENTER; break;
        case MINI_KEY_BACKSPACE: out->key = UI_BACKSPACE; break;
        case MINI_KEY_ESCAPE: out->key = UI_ESCAPE; break;
        case MINI_KEY_TAB: out->key = UI_TAB; break;
        default: break;
        }
    }
    return true;
}
mini_result_t ui_adapter_present(ui_adapter_t *a, const ui_frame_t *f)
{
    bool changed = !a->presented;
    for (unsigned r = 0; r < 7; ++r) {
        bool dirty = !a->presented || (a->last.inverse_row == (int)r) != (f->inverse_row == (int)r);
        for (unsigned c = 0; c < 20 && !dirty; ++c) dirty = a->last.rows[r][c] != f->rows[r][c];
        if (!dirty) continue;
        mini_result_t rc = a->display->text->write_at_attr(r, 0, f->rows[r], 20,
                           f->inverse_row == (int)r ? MINI_TEXT_ATTR_INVERSE : MINI_TEXT_ATTR_NONE);
        if (rc != MINI_OK) return rc;
        changed = true;
    }
    if (changed) {
        mini_result_t rc = a->display->present();
        if (rc != MINI_OK) return rc;
        a->last = *f; a->presented = true;
    }
    return MINI_OK;
}
void ui_adapter_close(ui_adapter_t *a)
{
    if (a->display) { (void)a->display->text->clear(); (void)a->display->present(); }
    a->display = NULL; a->input = NULL; a->presented = false;
}

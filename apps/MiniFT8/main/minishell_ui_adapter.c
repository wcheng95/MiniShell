#include "minishell_ui_adapter.h"

#include <stddef.h>
#include <string.h>

#define FIELD_END(type, field) \
    ((uint32_t)(offsetof(type, field) + sizeof(((type *)0)->field)))

bool minift8_ui_adapter_init(MiniFt8UiAdapter *adapter, const mini_api_t *api)
{
    if (adapter == NULL || api == NULL || api->api_version != MINISHELL_API_VERSION ||
        api->struct_size < FIELD_END(mini_api_t, input) ||
        api->display == NULL || api->input == NULL) {
        return false;
    }

    const mini_display_api_t *display = api->display;
    const mini_input_api_t *input = api->input;
    if ((display->capabilities & MINI_DISPLAY_CAP_TEXT) == 0u || display->text == NULL ||
        display->struct_size < FIELD_END(mini_display_api_t, present) ||
        display->present == NULL ||
        (input->capabilities & MINI_INPUT_CAP_KEY) == 0u || input->key == NULL) {
        return false;
    }

    const mini_text_display_api_t *text = display->text;
    const mini_key_input_api_t *key = input->key;
    if (text->struct_size < FIELD_END(mini_text_display_api_t, write_at) ||
        text->get_info == NULL || text->clear == NULL || text->write_at == NULL ||
        key->struct_size < FIELD_END(mini_key_input_api_t, read) || key->read == NULL) {
        return false;
    }

    mini_text_display_info_t info = {.struct_size = sizeof(info)};
    if (text->get_info(&info) != MINI_OK || info.columns < UI_COLS || info.rows < UI_ROWS) {
        return false;
    }

    adapter->display = display;
    adapter->text = text;
    adapter->key = key;
    return true;
}

bool minift8_ui_adapter_render(const MiniFt8UiAdapter *adapter, const UiFrame *frame)
{
    if (adapter == NULL || frame == NULL || adapter->text == NULL || adapter->display == NULL) {
        return false;
    }

    if (adapter->text->clear() != MINI_OK) return false;
    for (uint32_t row = 0u; row < UI_ROWS; ++row) {
        if (adapter->text->write_at(row, 0u, frame->rows[row], UI_COLS) != MINI_OK) {
            return false;
        }
    }
    return adapter->display->present() == MINI_OK;
}

bool minift8_ui_adapter_read_input(const MiniFt8UiAdapter *adapter, UiInput *out_input)
{
    if (adapter == NULL || adapter->key == NULL || out_input == NULL) return false;

    memset(out_input, 0, sizeof(*out_input));
    mini_key_event_t event = {.struct_size = sizeof(event)};
    if (adapter->key->read(&event, MINI_WAIT_FOREVER) != MINI_OK) return false;

    if (event.type == MINI_KEY_EVENT_CHAR) {
        if (event.codepoint == 'q' || event.codepoint == 'Q') {
            out_input->type = UI_INPUT_QUIT;
        } else if (event.codepoint == '`') {
            out_input->type = UI_INPUT_BACK;
        } else if (event.codepoint <= 255u) {
            out_input->type = UI_INPUT_CHAR;
            out_input->ch = (int)event.codepoint;
        }
        return true;
    }

    if (event.type != MINI_KEY_EVENT_SPECIAL) return true;

    switch (event.key) {
        case MINI_KEY_UP: out_input->type = UI_INPUT_UP; break;
        case MINI_KEY_DOWN: out_input->type = UI_INPUT_DOWN; break;
        case MINI_KEY_LEFT: out_input->type = UI_INPUT_LEFT; break;
        case MINI_KEY_RIGHT: out_input->type = UI_INPUT_RIGHT; break;
        case MINI_KEY_ENTER: out_input->type = UI_INPUT_ENTER; break;
        case MINI_KEY_ESCAPE: out_input->type = UI_INPUT_BACK; break;
        case MINI_KEY_PAGE_UP: out_input->type = UI_INPUT_PAGE_PREV; break;
        case MINI_KEY_PAGE_DOWN: out_input->type = UI_INPUT_PAGE_NEXT; break;
        default: out_input->type = UI_INPUT_NONE; break;
    }
    return true;
}

void minift8_ui_adapter_shutdown(const MiniFt8UiAdapter *adapter)
{
    if (adapter == NULL || adapter->text == NULL || adapter->display == NULL) return;
    (void)adapter->text->clear();
    (void)adapter->display->present();
}

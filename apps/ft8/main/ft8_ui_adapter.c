#include "ft8_ui_adapter.h"

#include <stddef.h>
#include <string.h>

#define FIELD_END(type, field) \
    ((uint32_t)(offsetof(type, field) + sizeof(((type *)0)->field)))

bool ft8_ui_adapter_init(ft8_ui_adapter_t *adapter, const mini_api_t *api,
                         ft8_presentation_profile_t presentation)
{
    if (adapter == NULL || api == NULL || api->api_version != MINISHELL_API_VERSION ||
        api->struct_size < FIELD_END(mini_api_t, input) ||
        api->display == NULL || api->input == NULL) {
        return false;
    }

    ft8_presentation_spec_t spec;
    if (!ft8_presentation_get_spec(presentation, &spec)) return false;

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
    if (text->get_info(&info) != MINI_OK ||
        info.columns < spec.columns || info.rows < spec.rows) {
        return false;
    }

    adapter->display = display;
    adapter->text = text;
    adapter->key = key;
    adapter->presentation = presentation;
    return true;
}

bool ft8_ui_adapter_render(const ft8_ui_adapter_t *adapter, const UiFrame *frame)
{
    if (adapter == NULL || frame == NULL || adapter->text == NULL || adapter->display == NULL) {
        return false;
    }

    ft8_presentation_spec_t spec;
    if (!ft8_presentation_get_spec(adapter->presentation, &spec) ||
        frame->column_count != spec.columns || frame->row_count != spec.rows ||
        frame->column_count > UI_MAX_COLS || frame->row_count > UI_MAX_ROWS) {
        return false;
    }

    if (adapter->text->clear() != MINI_OK) return false;
    for (uint32_t row = 0u; row < frame->row_count; ++row) {
        if (adapter->text->write_at(row, 0u, frame->rows[row], frame->column_count) != MINI_OK) {
            return false;
        }
    }
    return adapter->display->present() == MINI_OK;
}

static void translate_event(const mini_key_event_t *event, UiInput *out_input)
{
    if (event->type == MINI_KEY_EVENT_CHAR) {
        if (event->codepoint == 'q' || event->codepoint == 'Q') {
            out_input->type = UI_INPUT_QUIT;
        } else if (event->codepoint == '`') {
            out_input->type = UI_INPUT_BACK;
        } else if (event->codepoint <= 255u) {
            out_input->type = UI_INPUT_CHAR;
            out_input->ch = (int)event->codepoint;
        }
        return;
    }

    if (event->type != MINI_KEY_EVENT_SPECIAL) return;

    switch (event->key) {
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
}

bool ft8_ui_adapter_read_input_timeout(const ft8_ui_adapter_t *adapter,
                                       uint32_t timeout_ms,
                                       UiInput *out_input,
                                       bool *out_has_input)
{
    mini_result_t result;
    mini_key_event_t event = {.struct_size = sizeof(event)};

    if (adapter == NULL || adapter->key == NULL || out_input == NULL || out_has_input == NULL)
        return false;

    memset(out_input, 0, sizeof(*out_input));
    *out_has_input = false;
    result = adapter->key->read(&event, timeout_ms);
    if (result == MINI_ERR_NOT_READY || result == MINI_ERR_TIMEOUT) return true;
    if (result != MINI_OK) return false;

    translate_event(&event, out_input);
    *out_has_input = true;
    return true;
}

bool ft8_ui_adapter_read_input(const ft8_ui_adapter_t *adapter, UiInput *out_input)
{
    bool has_input = false;
    if (!ft8_ui_adapter_read_input_timeout(adapter, MINI_WAIT_FOREVER,
                                           out_input, &has_input)) {
        return false;
    }
    return has_input;
}

void ft8_ui_adapter_shutdown(const ft8_ui_adapter_t *adapter)
{
    if (adapter == NULL || adapter->text == NULL || adapter->display == NULL) return;
    (void)adapter->text->clear();
    (void)adapter->display->present();
}

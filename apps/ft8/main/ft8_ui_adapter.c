#include "ft8_ui_adapter.h"

#include <stddef.h>
#include <string.h>

#define FIELD_END(type, field) \
    ((uint32_t)(offsetof(type, field) + sizeof(((type *)0)->field)))

struct Ft8WaterfallHistory {
    uint8_t rows[FT8_WATERFALL_HEIGHT][FT8_WATERFALL_WIDTH];
    uint32_t head;
    bool visible;
};

static bool draw_waterfall(const ft8_ui_adapter_t *adapter)
{
    uint16_t pixels[FT8_WATERFALL_WIDTH];
    const Ft8WaterfallHistory *history = adapter->waterfall;
    for (uint32_t y = 0; y < FT8_WATERFALL_HEIGHT; ++y) {
        const uint8_t *row = history->rows[(history->head + y) % FT8_WATERFALL_HEIGHT];
        for (uint32_t x = 0; x < FT8_WATERFALL_WIDTH; ++x) {
            uint16_t v = row[x];
            pixels[x] = ((v & 0xF8u) << 8) | ((v & 0xFCu) << 3);
        }
        if (adapter->raster->blit_rgb565(0, y, FT8_WATERFALL_WIDTH, 1,
                                        pixels, FT8_WATERFALL_WIDTH) != MINI_OK) return false;
    }
    return true;
}

bool ft8_ui_adapter_waterfall_supported(const ft8_ui_adapter_t *adapter)
{
    return adapter && adapter->waterfall;
}

void ft8_ui_adapter_push_waterfall(ft8_ui_adapter_t *adapter, const uint8_t row[FT8_WATERFALL_WIDTH])
{
    if (!adapter || !adapter->waterfall || !row) return;
    Ft8WaterfallHistory *history = adapter->waterfall;
    memcpy(history->rows[history->head], row, FT8_WATERFALL_WIDTH);
    history->head = (history->head + 1u) % FT8_WATERFALL_HEIGHT;
    if (history->visible) (void)draw_waterfall(adapter);
}

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

    memset(adapter, 0, sizeof(*adapter));
    adapter->display = display;
    adapter->text = text;
    adapter->key = key;
    adapter->presentation = presentation;
    if ((display->capabilities & MINI_DISPLAY_CAP_RASTER_RGB565) &&
        display->struct_size >= FIELD_END(mini_display_api_t, raster) && display->raster &&
        display->raster->struct_size >= FIELD_END(mini_raster_display_api_t, blit_rgb565) &&
        display->raster->get_info && display->raster->blit_rgb565 &&
        api->memory && api->memory->struct_size >= FIELD_END(mini_memory_api_t, free) &&
        api->memory->alloc && api->memory->free) {
        mini_raster_display_info_t raster_info = {.struct_size = sizeof(raster_info)};
        if (display->raster->get_info(&raster_info) == MINI_OK &&
            raster_info.width >= FT8_WATERFALL_WIDTH && raster_info.height >= FT8_WATERFALL_HEIGHT) {
            adapter->memory = api->memory;
            adapter->raster = display->raster;
        }
    }
    return true;
}

bool ft8_ui_adapter_waterfall_prepare(ft8_ui_adapter_t *adapter)
{
    if (!adapter || !adapter->raster || !adapter->memory) return false;
    if (adapter->waterfall) return true;
    void *allocation = NULL;
    if (adapter->memory->alloc(sizeof(Ft8WaterfallHistory), &allocation) != MINI_OK || !allocation)
        return false;
    adapter->waterfall = allocation;
    memset(allocation, 0, sizeof(Ft8WaterfallHistory));
    return true;
}

static uint32_t foreground(UiColor color)
{
    switch (color) {
        case UI_COLOR_RED: return MINI_TEXT_ATTR_FG_RED;
        case UI_COLOR_GREEN: return MINI_TEXT_ATTR_FG_GREEN;
        case UI_COLOR_DEFAULT: return MINI_TEXT_ATTR_FG_DEFAULT;
        default: return MINI_TEXT_ATTR_FG_WHITE;
    }
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

    const mini_text_display_api_t *text = adapter->text;
    const uint64_t caps = adapter->display->capabilities;
    const bool color = (caps & MINI_DISPLAY_CAP_TEXT_COLOR) &&
        text->struct_size >= FIELD_END(mini_text_display_api_t, write_at_attr) && text->write_at_attr;
    const bool separator = (caps & MINI_DISPLAY_CAP_ROW_SEPARATOR) &&
        text->struct_size >= FIELD_END(mini_text_display_api_t, set_row_separator) && text->set_row_separator;
    /* Raster owns the top row while overlaid; full clear would dirty it too. */
    if (!frame->waterfall_overlay && text->clear() != MINI_OK) return false;
    for (uint32_t row = frame->waterfall_overlay ? 1u : 0u; row < frame->row_count; ++row) {
        mini_result_t result = MINI_ERR_UNSUPPORTED;
        if (color) result = text->write_at_attr(row, 0u, frame->rows[row], frame->column_count,
                                               foreground(frame->row_color[row]));
        if (result == MINI_ERR_UNSUPPORTED)
            result = text->write_at(row, 0u, frame->rows[row], frame->column_count);
        if (result != MINI_OK) return false;
    }
    /* Full clear resets decorations; the overlay path must also remove an old
     * separator explicitly when the new frame no longer requests one. */
    if (separator && (frame->separator_after_top || frame->waterfall_overlay)) {
        mini_result_t result = text->set_row_separator(0u, frame->separator_after_top
            ? foreground(frame->separator_color) : MINI_TEXT_ATTR_FG_DEFAULT);
        if (result != MINI_OK && result != MINI_ERR_UNSUPPORTED) return false;
    }
    if (adapter->display->present() != MINI_OK) return false;
    if (adapter->waterfall) {
        adapter->waterfall->visible = frame->waterfall_overlay;
        if (frame->waterfall_overlay) return draw_waterfall(adapter);
    }
    return true;
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
    if (adapter->waterfall) (void)adapter->memory->free(adapter->waterfall);
    (void)adapter->text->clear();
    (void)adapter->display->present();
}

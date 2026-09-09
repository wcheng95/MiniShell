#include <stddef.h>
#include <string.h>

#include "minishell/api.h"

static mini_result_t write_line(const mini_text_display_api_t *text,
                                uint32_t row,
                                uint32_t columns,
                                const char *value)
{
    uint32_t length = (uint32_t)strlen(value);
    if (length > columns) length = columns;
    return text->write_at(row, 0u, value, length);
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    const mini_api_t *api = mini_api_get();
    if (api == NULL || api->api_version != MINISHELL_API_VERSION ||
        api->display == NULL ||
        (api->display->capabilities & MINI_DISPLAY_CAP_TEXT) == 0u ||
        api->display->text == NULL ||
        api->display->text->get_info == NULL ||
        api->display->text->clear == NULL ||
        api->display->text->write_at == NULL ||
        api->display->present == NULL ||
        api->input == NULL ||
        (api->input->capabilities & MINI_INPUT_CAP_KEY) == 0u ||
        api->input->key == NULL ||
        api->input->key->read == NULL) {
        return 2;
    }

    mini_text_display_info_t info = {.struct_size = sizeof(info)};
    if (api->display->text->get_info(&info) != MINI_OK ||
        info.columns == 0u || info.rows == 0u) {
        return 3;
    }

    static const char greeting[] = "Hello from MiniShell.";
    static const char footer[] = "q/Enter to exit";

    if (api->display->text->clear() != MINI_OK ||
        write_line(api->display->text, 0u, info.columns, greeting) != MINI_OK) {
        return 4;
    }

    if (info.rows > 1u &&
        write_line(api->display->text, info.rows - 1u, info.columns, footer) != MINI_OK) {
        return 4;
    }

    if (api->display->present() != MINI_OK) {
        return 4;
    }

    for (;;) {
        mini_key_event_t event = {.struct_size = sizeof(event)};
        mini_result_t result = api->input->key->read(&event, MINI_WAIT_FOREVER);
        if (result != MINI_OK) return 5;

        if (event.type == MINI_KEY_EVENT_CHAR &&
            (event.codepoint == (uint32_t)'q' || event.codepoint == (uint32_t)'Q')) {
            return 0;
        }
        if (event.type == MINI_KEY_EVENT_SPECIAL &&
            (event.key == MINI_KEY_ENTER || event.key == MINI_KEY_ESCAPE)) {
            return 0;
        }
    }
}

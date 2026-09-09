#include <stddef.h>
#include <string.h>

#include "minishell/api.h"

static int write_display_greeting(const mini_api_t *api)
{
    if (api->display == NULL ||
        (api->display->capabilities & MINI_DISPLAY_CAP_TEXT) == 0u ||
        api->display->text == NULL ||
        api->display->text->get_info == NULL ||
        api->display->text->clear == NULL ||
        api->display->text->write_at == NULL ||
        api->display->present == NULL) {
        return 0;
    }

    mini_text_display_info_t info = {.struct_size = sizeof(info)};
    if (api->display->text->get_info(&info) != MINI_OK ||
        info.columns == 0u || info.rows == 0u) {
        return 0;
    }

    static const char full[] = "Hello from MiniShell.";
    static const char compact[] = "Hello from MiniShell";
    const char *text = info.columns >= sizeof(full) - 1u ? full : compact;
    uint32_t length = (uint32_t)strlen(text);
    if (length > info.columns) length = info.columns;

    if (api->display->text->clear() != MINI_OK ||
        api->display->text->write_at(0u, 0u, text, length) != MINI_OK ||
        api->display->present() != MINI_OK) {
        return 0;
    }

    return 1;
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    const mini_api_t *api = mini_api_get();
    if (api == NULL || api->api_version != MINISHELL_API_VERSION) {
        return 2;
    }

    if (write_display_greeting(api)) {
        return 0;
    }

    if (api->system == NULL || api->system->write == NULL) {
        return 2;
    }

    api->system->write("Hello from MiniShell.\n");
    return 0;
}

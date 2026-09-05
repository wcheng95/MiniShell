#include "abi_test.h"

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    const mini_api_t *api = NULL;
    int rc = abi_test_base(&api);
    if (rc != 0) return rc;

    if (!ABI_HAS_API_FIELD(api, display) || api->display == NULL)
        return abi_test_skip(api, "abi_display", "display service unavailable");

    const mini_display_api_t *display = api->display;
    if (display->struct_size < ABI_FIELD_END(mini_display_api_t, present) ||
        display->present == NULL)
        return abi_test_fail(api, "abi_display", "v0 table incomplete", 40);

    if ((display->capabilities & MINI_DISPLAY_CAP_TEXT) == 0u || display->text == NULL) {
        if (display->present() != MINI_OK)
            return abi_test_fail(api, "abi_display", "present failed", 41);
        return abi_test_skip(api, "abi_display", "text capability unavailable");
    }

    const mini_text_display_api_t *text = display->text;
    if (text->struct_size < ABI_FIELD_END(mini_text_display_api_t, write_at) ||
        text->get_info == NULL || text->clear == NULL ||
        text->clear_at == NULL || text->write_at == NULL)
        return abi_test_fail(api, "abi_display", "text table incomplete", 42);

    mini_text_display_info_t info = {0};
    info.struct_size = sizeof(info);
    if (text->get_info(&info) != MINI_OK || info.columns == 0u || info.rows == 0u)
        return abi_test_fail(api, "abi_display", "get_info failed", 43);

    static const char message[] = "MiniShell Display ABI PASS";
    if (text->clear() != MINI_OK ||
        text->write_at(0u, 0u, message, sizeof(message) - 1u) != MINI_OK ||
        display->present() != MINI_OK)
        return abi_test_fail(api, "abi_display", "draw/present failed", 44);

    if (text->write_at(info.rows, 0u, message, 1u) != MINI_ERR_INVALID)
        return abi_test_fail(api, "abi_display", "bounds contract", 45);

    abi_test_line(api, "abi_display", "PASS");
    return 0;
}

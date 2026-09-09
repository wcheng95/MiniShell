#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "minishell/api.h"

static int fail(const mini_api_t *api, const char *message, int code)
{
    if (api != NULL && api->system != NULL && api->system->write != NULL) {
        api->system->write("a2_probe: FAIL: ");
        api->system->write(message);
        api->system->write("\n");
    }
    return code;
}

static mini_result_t row_write(const mini_text_display_api_t *text,
                               uint32_t row, uint32_t columns, const char *line)
{
    mini_result_t result = text->clear_at(row, 0u, 1u, columns);
    if (result != MINI_OK) return result;
    uint32_t count = (uint32_t)strlen(line);
    if (count > columns) count = columns;
    return text->write_at(row, 0u, line, count);
}

int main(int argc, char **argv)
{
    (void)argv;
    if (argc != 1) return 2;

    const mini_api_t *api = mini_api_get();
    if (api == NULL || api->api_version != MINISHELL_API_VERSION ||
        api->system == NULL || api->system->write == NULL) {
        return 2;
    }
    if (api->memory == NULL || api->display == NULL || api->display->text == NULL ||
        api->input == NULL || api->input->key == NULL) {
        return fail(api, "missing A2 service", 3);
    }

    void *memory = NULL;
    if (api->memory->alloc(128u, &memory) != MINI_OK || memory == NULL) {
        return fail(api, "memory alloc", 4);
    }
    memset(memory, 0x5a, 128u);

    void *larger = NULL;
    if (api->memory->realloc(memory, 256u, &larger) != MINI_OK || larger == NULL) {
        return fail(api, "memory realloc", 5);
    }

    mini_memory_info_t memory_info = {.struct_size = sizeof(memory_info)};
    if (api->memory->get_info(&memory_info) != MINI_OK ||
        (memory_info.valid_fields & MINI_MEM_INFO_APP_USAGE) == 0u ||
        memory_info.app_allocation_count != 1u || memory_info.app_allocated_bytes != 256u) {
        return fail(api, "memory accounting", 6);
    }
    if (api->memory->free(larger) != MINI_OK) {
        return fail(api, "memory free", 7);
    }

    const mini_text_display_api_t *text = api->display->text;
    mini_text_display_info_t info = {.struct_size = sizeof(info)};
    if ((api->display->capabilities & MINI_DISPLAY_CAP_TEXT) == 0u ||
        text->get_info(&info) != MINI_OK || info.columns == 0u || info.rows < 6u) {
        return fail(api, "display info", 8);
    }

    if ((api->input->capabilities & MINI_INPUT_CAP_KEY) == 0u ||
        api->input->key->read == NULL) {
        return fail(api, "input capability", 9);
    }

    char geometry[32];
    (void)snprintf(geometry, sizeof(geometry), "Display %lux%lu",
                   (unsigned long)info.columns, (unsigned long)info.rows);

    if (text->clear() != MINI_OK ||
        row_write(text, 0u, info.columns, "MiniShell A2 probe") != MINI_OK ||
        row_write(text, 1u, info.columns, geometry) != MINI_OK ||
        row_write(text, 2u, info.columns, "Memory PASS") != MINI_OK) {
        return fail(api, "display write", 10);
    }

    const char inverse[] = " inverse test ";
    if (text->write_at_attr != NULL) {
        if (text->write_at_attr(3u, 0u, inverse, (uint32_t)(sizeof(inverse) - 1u),
                                MINI_TEXT_ATTR_INVERSE) != MINI_OK) {
            return fail(api, "inverse text", 11);
        }
    } else if (row_write(text, 3u, info.columns, "inverse unavailable") != MINI_OK) {
        return fail(api, "display fallback", 12);
    }

    if (row_write(text, 4u, info.columns, "Press x for Input") != MINI_OK ||
        row_write(text, 5u, info.columns, "q cancels") != MINI_OK ||
        api->display->present() != MINI_OK) {
        return fail(api, "display present", 13);
    }

    for (;;) {
        mini_key_event_t event = {.struct_size = sizeof(event)};
        mini_result_t result = api->input->key->read(&event, MINI_WAIT_FOREVER);
        if (result != MINI_OK) return fail(api, "input read", 14);

        if (event.type == MINI_KEY_EVENT_CHAR &&
            (event.codepoint == (uint32_t)'q' || event.codepoint == (uint32_t)'Q')) {
            api->system->write("a2_probe: canceled\n");
            return 0;
        }
        if (event.type == MINI_KEY_EVENT_CHAR &&
            (event.codepoint == (uint32_t)'x' || event.codepoint == (uint32_t)'X')) {
            break;
        }
    }

    if (row_write(text, 4u, info.columns, "Input PASS") != MINI_OK ||
        row_write(text, 5u, info.columns, "ENTER exits") != MINI_OK ||
        api->display->present() != MINI_OK) {
        return fail(api, "input result display", 15);
    }

    for (;;) {
        mini_key_event_t event = {.struct_size = sizeof(event)};
        mini_result_t result = api->input->key->read(&event, MINI_WAIT_FOREVER);
        if (result != MINI_OK) return fail(api, "exit input", 16);
        if ((event.type == MINI_KEY_EVENT_SPECIAL && event.key == MINI_KEY_ENTER) ||
            (event.type == MINI_KEY_EVENT_CHAR &&
             (event.codepoint == (uint32_t)'q' || event.codepoint == (uint32_t)'Q'))) {
            break;
        }
    }

    api->system->write("a2_probe: PASS\n");
    return 0;
}

#include <stddef.h>
#include <string.h>

#include "minishell/api.h"

static int read_event(const mini_api_t *api, mini_key_event_t *event)
{
    *event = (mini_key_event_t){.struct_size = sizeof(*event)};
    return api->input->key->read(event, 2000u) == MINI_OK ? 0 : -1;
}

static int expect_special(const mini_api_t *api, uint32_t key)
{
    mini_key_event_t event;
    if (read_event(api, &event) != 0) return -1;
    return event.type == MINI_KEY_EVENT_SPECIAL && event.key == key &&
           event.codepoint == 0u && event.modifiers == 0u ? 0 : -1;
}

static int expect_char(const mini_api_t *api, uint32_t codepoint)
{
    mini_key_event_t event;
    if (read_event(api, &event) != 0) return -1;
    return event.type == MINI_KEY_EVENT_CHAR && event.codepoint == codepoint &&
           event.key == 0u && event.modifiers == 0u ? 0 : -1;
}

static int run_h5_probe(const mini_api_t *api)
{
    if (expect_special(api, MINI_KEY_UP) != 0) return 10;
    if (expect_special(api, MINI_KEY_PAGE_DOWN) != 0) return 11;
    if (expect_char(api, 0x4E2Du) != 0) return 12;
    if (expect_special(api, MINI_KEY_ESCAPE) != 0) return 13;
    return 0;
}

int main(int argc, char **argv)
{
    const mini_api_t *api = mini_api_get();
    if (api == NULL || api->system == NULL || api->system->write == NULL ||
        api->input == NULL || api->input->key == NULL ||
        api->input->key->read == NULL) {
        return 2;
    }

    api->system->write("input_probe: READY\n");

    if (argc > 1 && strcmp(argv[1], "h5") == 0) {
        int result = run_h5_probe(api);
        if (result != 0) {
            api->system->write("input_probe: FAIL h5\n");
            return result;
        }
        api->system->write("input_probe: PASS\n");
        return 0;
    }

    mini_key_event_t event;
    if (read_event(api, &event) != 0) {
        api->system->write("input_probe: FAIL read\n");
        return 3;
    }

    if (event.type != MINI_KEY_EVENT_CHAR || event.codepoint != (uint32_t)'x' ||
        event.modifiers != 0u) {
        api->system->write("input_probe: FAIL event\n");
        return 4;
    }

    api->system->write("input_probe: PASS\n");
    return 0;
}

#include "minishell/api.h"

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    const mini_api_t *api = mini_api_get();
    if (api == NULL || api->system == NULL || api->system->write == NULL ||
        api->input == NULL || api->input->key == NULL ||
        api->input->key->read == NULL) {
        return 2;
    }

    api->system->write("input_probe: READY\n");

    mini_key_event_t event = {.struct_size = sizeof(event)};
    mini_result_t result = api->input->key->read(&event, 2000u);
    if (result != MINI_OK) {
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

#include "abi_test.h"

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    const mini_api_t *api = NULL;
    int rc = abi_test_base(&api);
    if (rc != 0) return rc;

    if (!ABI_HAS_API_FIELD(api, input) || api->input == NULL)
        return abi_test_skip(api, "abi_input", "input service unavailable");

    const mini_input_api_t *input = api->input;
    if (input->struct_size < ABI_FIELD_END(mini_input_api_t, key))
        return abi_test_fail(api, "abi_input", "v0 table incomplete", 50);

    if ((input->capabilities & MINI_INPUT_CAP_KEY) == 0u || input->key == NULL)
        return abi_test_skip(api, "abi_input", "key capability unavailable");

    const mini_key_input_api_t *key = input->key;
    if (key->struct_size < ABI_FIELD_END(mini_key_input_api_t, read) || key->read == NULL)
        return abi_test_fail(api, "abi_input", "key table incomplete", 51);

    mini_key_event_t too_small = {0};
    too_small.struct_size = sizeof(uint32_t);
    if (key->read(&too_small, MINI_WAIT_NONE) != MINI_ERR_INVALID)
        return abi_test_fail(api, "abi_input", "struct_size contract", 52);

    mini_key_event_t event = {0};
    event.struct_size = sizeof(event);
    mini_result_t r = key->read(&event, MINI_WAIT_NONE);
    if (r != MINI_OK && r != MINI_ERR_NOT_READY)
        return abi_test_fail(api, "abi_input", "nonblocking read result", 53);

    abi_test_line(api, "abi_input", "PASS");
    return 0;
}

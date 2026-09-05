#include "abi_test.h"

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    const mini_api_t *api = NULL;
    int rc = abi_test_base(&api);
    if (rc != 0) return rc;

    api->system->write("[abi_system] binary ABI call path OK\n");
    abi_test_line(api, "abi_system", "PASS");
    return 0;
}

#include "abi_test.h"

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    const mini_api_t *api = NULL;
    int rc = abi_test_base(&api);
    if (rc != 0) return rc;

    if (!ABI_HAS_API_FIELD(api, memory) || api->memory == NULL)
        return abi_test_fail(api, "abi_memory", "service unavailable", 10);

    const mini_memory_api_t *mem = api->memory;
    if (mem->struct_size < ABI_FIELD_END(mini_memory_api_t, get_info) ||
        mem->alloc == NULL || mem->realloc == NULL ||
        mem->free == NULL || mem->get_info == NULL)
        return abi_test_fail(api, "abi_memory", "v0 table incomplete", 11);

    void *bad = (void *)(uintptr_t)1u;
    if (mem->alloc(0u, &bad) != MINI_ERR_INVALID || bad != NULL)
        return abi_test_fail(api, "abi_memory", "alloc(0) contract", 12);

    void *ptr = NULL;
    if (mem->alloc(32u, &ptr) != MINI_OK || ptr == NULL)
        return abi_test_fail(api, "abi_memory", "alloc failed", 13);

    uint8_t *bytes = (uint8_t *)ptr;
    for (uint32_t i = 0; i < 32u; ++i) bytes[i] = (uint8_t)(i ^ 0x5Au);

    mini_memory_info_t info = {0};
    info.struct_size = sizeof(info);
    if (mem->get_info(&info) != MINI_OK ||
        (info.valid_fields & MINI_MEM_INFO_APP_USAGE) == 0u ||
        info.app_allocation_count != 1u ||
        info.app_allocated_bytes != 32u)
        return abi_test_fail(api, "abi_memory", "app accounting mismatch", 14);

    void *grown = NULL;
    if (mem->realloc(ptr, 64u, &grown) != MINI_OK || grown == NULL)
        return abi_test_fail(api, "abi_memory", "realloc failed", 15);

    bytes = (uint8_t *)grown;
    for (uint32_t i = 0; i < 32u; ++i) {
        if (bytes[i] != (uint8_t)(i ^ 0x5Au))
            return abi_test_fail(api, "abi_memory", "realloc lost data", 16);
    }

    if (mem->free(grown) != MINI_OK || mem->free(NULL) != MINI_OK)
        return abi_test_fail(api, "abi_memory", "free contract", 17);

    abi_test_line(api, "abi_memory", "PASS");
    return 0;
}

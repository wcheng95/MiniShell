#pragma once

#include <stddef.h>
#include <stdint.h>

#include "minishell/api.h"

#define ABI_FIELD_END(type, field) \
    ((uint32_t)(offsetof(type, field) + sizeof(((type *)0)->field)))

#define ABI_HAS_API_FIELD(api, field) \
    ((api)->struct_size >= ABI_FIELD_END(mini_api_t, field))

static inline int abi_test_base(const mini_api_t **out_api)
{
    const mini_api_t *api = mini_api_get();
    if (api == NULL) return 2;
    if (api->abi_version != MINISHELL_ABI_VERSION) return 3;
    if (!ABI_HAS_API_FIELD(api, system) || api->system == NULL) return 4;
    if (api->system->struct_size < ABI_FIELD_END(mini_system_api_t, write) ||
        api->system->write == NULL) return 5;
    *out_api = api;
    return 0;
}

static inline void abi_test_line(const mini_api_t *api,
                                 const char *name,
                                 const char *result)
{
    api->system->write("[");
    api->system->write(name);
    api->system->write("] ");
    api->system->write(result);
    api->system->write("\n");
}

static inline int abi_test_fail(const mini_api_t *api,
                                const char *name,
                                const char *reason,
                                int code)
{
    api->system->write("[");
    api->system->write(name);
    api->system->write("] FAIL: ");
    api->system->write(reason);
    api->system->write("\n");
    return code;
}

static inline int abi_test_skip(const mini_api_t *api,
                                const char *name,
                                const char *reason)
{
    api->system->write("[");
    api->system->write(name);
    api->system->write("] SKIP: ");
    api->system->write(reason);
    api->system->write("\n");
    return 0;
}

#include "minishell/api.h"
#include "platform_backend.h"

static void system_write(const char *text)
{
    if (text != NULL) {
        minishell_platform_write(text);
    }
}

static const mini_system_api_t s_system_api = {
    .struct_size = sizeof(mini_system_api_t),
    .write = system_write,
};

static const mini_api_t s_api = {
    .abi_version = MINISHELL_ABI_VERSION,
    .struct_size = sizeof(mini_api_t),
    .system = &s_system_api,
    .memory = NULL,
    .fs = NULL,
    .time_location = NULL,
    .display = NULL,
    .input = NULL,
};

const mini_api_t *mini_api_get(void)
{
    return &s_api;
}

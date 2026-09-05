#include <stdio.h>

#include "minishell/api.h"

static void system_write(const char *text)
{
    if (text == NULL) {
        return;
    }

    fputs(text, stdout);
    fflush(stdout);
}

static const mini_system_api_t s_system_api = {
    .struct_size = sizeof(mini_system_api_t),
    .write = system_write,
};

static const mini_api_t s_api = {
    .abi_version = MINISHELL_ABI_VERSION,
    .struct_size = sizeof(mini_api_t),
    .system = &s_system_api,
};

const mini_api_t *mini_api_get(void)
{
    return &s_api;
}

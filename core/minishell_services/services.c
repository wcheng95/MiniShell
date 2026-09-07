#include <string.h>

#include "services_internal.h"

static minishell_services_port_t s_port;
static minishell_resource_limits_t s_limits;

static mini_api_t s_api = {
    .abi_version = MINISHELL_ABI_VERSION,
    .struct_size = sizeof(mini_api_t),
    .system = NULL,
    .memory = NULL,
    .fs = NULL,
    .time_location = NULL,
    .display = NULL,
    .input = NULL,
    .audio = NULL,
};

const minishell_services_port_t *minishell_services_port(void)
{
    return &s_port;
}

uint64_t minishell_memory_limit_bytes(void)
{
    return s_limits.memory_bytes;
}

uint64_t minishell_storage_limit_bytes(void)
{
    return s_limits.storage_bytes;
}

void minishell_services_set_resource_limits(const minishell_resource_limits_t *limits)
{
    memset(&s_limits, 0, sizeof(s_limits));
    if (limits != NULL) {
        s_limits = *limits;
    }
}

static void refresh_api_table(void)
{
    s_api.abi_version = MINISHELL_ABI_VERSION;
    s_api.struct_size = sizeof(mini_api_t);
    s_api.system = minishell_system_service_api();
    s_api.memory = minishell_memory_service_available() ? minishell_memory_service_api() : NULL;
    s_api.fs = minishell_filesystem_service_available() ? minishell_filesystem_service_api() : NULL;
    s_api.time_location = minishell_time_location_service_available()
                              ? minishell_time_location_service_api()
                              : NULL;
    s_api.display = minishell_display_service_available() ? minishell_display_service_api() : NULL;
    s_api.input = minishell_input_service_available() ? minishell_input_service_api() : NULL;
    s_api.audio = minishell_audio_service_available() ? minishell_audio_service_api() : NULL;
}

void minishell_services_configure(const minishell_services_port_t *port)
{
    /* Clean up through the old port before replacing its callbacks. */
    minishell_services_app_end();

    memset(&s_port, 0, sizeof(s_port));
    if (port != NULL) {
        s_port = *port;
    }

    minishell_memory_service_configure();
    minishell_filesystem_service_configure();
    minishell_time_location_service_configure();
    minishell_display_service_configure();
    minishell_input_service_configure();
    minishell_audio_service_configure();
    refresh_api_table();
}

void minishell_services_app_begin(void)
{
    minishell_memory_service_app_begin();
    minishell_filesystem_service_app_begin();
    minishell_input_service_app_begin();
    minishell_audio_service_app_begin();
}

void minishell_services_app_end(void)
{
    minishell_audio_service_app_end();
    minishell_filesystem_service_app_end();
    minishell_memory_service_app_end();
    minishell_input_service_app_end();
}

const mini_api_t *mini_api_get(void)
{
    if (s_api.system == NULL) {
        refresh_api_table();
    }
    return &s_api;
}

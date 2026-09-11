#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "linux_internal.h"
#include "minishell_services.h"
#include "platform_backend.h"

static minishell_services_port_t s_services_port;

static void system_write(void *ctx, const char *text)
{
    (void)ctx;
    minishell_platform_console_write(text);
}

static void console_write(void *ctx, const char *text)
{
    (void)ctx;
    minishell_platform_console_write(text);
}

static void *memory_alloc(void *ctx, uint32_t size)
{
    (void)ctx;
    return malloc((size_t)size);
}

static void *memory_realloc(void *ctx, void *ptr, uint32_t new_size)
{
    (void)ctx;
    return realloc(ptr, (size_t)new_size);
}

static void memory_free(void *ctx, void *ptr)
{
    (void)ctx;
    free(ptr);
}

static bool memory_get_info(void *ctx, uint64_t *free_bytes,
                            uint64_t *largest_free_block)
{
    (void)ctx;
    (void)free_bytes;
    (void)largest_free_block;

    /* Linux has no useful equivalent of an MCU largest-free-block value.
     * The resident Memory service still reports exact per-app accounting. */
    return false;
}

static void configure_services_port(void)
{
    memset(&s_services_port, 0, sizeof(s_services_port));

    s_services_port.system_write = system_write;
    s_services_port.console_write = console_write;
    s_services_port.memory_alloc = memory_alloc;
    s_services_port.memory_realloc = memory_realloc;
    s_services_port.memory_free = memory_free;
    s_services_port.memory_get_info = memory_get_info;

    linux_filesystem_configure(&s_services_port);
    linux_time_location_configure(&s_services_port);
    linux_terminal_configure(&s_services_port);
    linux_digital_io_configure(&s_services_port);
}

int minishell_platform_init(void)
{
    linux_console_prepare();

    int result = linux_paths_init();
    if (result != 0) return result;

    result = linux_prepare_logical_root();
    if (result != 0) return result;

    configure_services_port();
    return 0;
}

void minishell_platform_shutdown(void)
{
    linux_terminal_app_end();
}

const minishell_services_port_t *minishell_platform_services_port(void)
{
    return &s_services_port;
}

minishell_platform_result_t minishell_platform_apps_list(minishell_app_emit_fn emit,
                                                         void *ctx)
{
    return linux_loader_apps_list(emit, ctx);
}

minishell_platform_result_t minishell_platform_app_run(const char *name,
                                                       int argc,
                                                       char **argv,
                                                       int *out_app_result)
{
    return linux_loader_app_run(name, argc, argv, out_app_result);
}

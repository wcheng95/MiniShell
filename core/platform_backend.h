#pragma once

#include <stddef.h>
#include <stdint.h>

#include "minishell_services.h"

typedef void (*minishell_app_emit_fn)(const char *name, void *ctx);

typedef int32_t minishell_platform_result_t;

#define MINISHELL_PLATFORM_OK                 ((minishell_platform_result_t) 0)
#define MINISHELL_PLATFORM_ERR_INVALID        ((minishell_platform_result_t)-1)
#define MINISHELL_PLATFORM_ERR_NOT_FOUND      ((minishell_platform_result_t)-2)
#define MINISHELL_PLATFORM_ERR_LOAD           ((minishell_platform_result_t)-3)
#define MINISHELL_PLATFORM_ERR_IO             ((minishell_platform_result_t)-4)
#define MINISHELL_PLATFORM_ERR_NO_MEMORY      ((minishell_platform_result_t)-5)
#define MINISHELL_PLATFORM_ERR_NAME_TOO_LONG  ((minishell_platform_result_t)-6)

int minishell_platform_init(void);
void minishell_platform_shutdown(void);
const char *minishell_platform_name(void);
int minishell_platform_resource_limits(minishell_resource_limits_t *out_limits);

/* Private resident-shell console. Applications must use MiniShell public
 * Display/Input/System APIs instead of this interface. */
void minishell_platform_console_write(const char *text);
int minishell_platform_console_read_line(char *buffer, size_t capacity);

const minishell_services_port_t *minishell_platform_services_port(void);
void minishell_platform_services_prepare(minishell_services_port_t *out_port);

minishell_platform_result_t minishell_platform_apps_list(minishell_app_emit_fn emit,
                                                         void *ctx);
minishell_platform_result_t minishell_platform_app_run(const char *name,
                                                       int argc,
                                                       char **argv,
                                                       int *out_app_result);

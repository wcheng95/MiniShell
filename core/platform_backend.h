#pragma once

#include <stddef.h>

#include "minishell_services.h"

typedef void (*minishell_app_emit_fn)(const char *name, void *ctx);

int minishell_platform_init(void);
void minishell_platform_shutdown(void);
void minishell_platform_write(const char *text);
const char *minishell_platform_name(void);

const minishell_services_port_t *minishell_platform_services_port(void);

int minishell_platform_apps_list(minishell_app_emit_fn emit, void *ctx);
int minishell_platform_app_run(const char *name, int argc, char **argv);

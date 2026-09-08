#pragma once

#include <stddef.h>

#include "minishell_services.h"
#include "platform_backend.h"

mini_result_t linux_result_from_errno(int error);

void linux_console_prepare(void);

int linux_paths_init(void);
int linux_prepare_logical_root(void);
const char *linux_root_dir(void);
const char *linux_app_dir(void);
mini_result_t linux_host_path(const char *logical, char *out, size_t out_size);

void linux_filesystem_configure(minishell_services_port_t *port);
void linux_time_location_configure(minishell_services_port_t *port);
void linux_terminal_configure(minishell_services_port_t *port);

int linux_terminal_app_begin(void);
void linux_terminal_app_end(void);

minishell_platform_result_t linux_loader_apps_list(minishell_app_emit_fn emit,
                                                   void *ctx);
minishell_platform_result_t linux_loader_app_run(const char *name,
                                                 int argc,
                                                 char **argv,
                                                 int *out_app_result);

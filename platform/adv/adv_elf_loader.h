#pragma once

#include <stdbool.h>

#include "platform_backend.h"

/* Private ADV runtime-ELF boundary. Application discovery/resolution policy
 * remains in adv_apps.c; this module only enumerates, probes, loads, runs, and
 * unloads ELF applications from one filesystem root. */
bool adv_elf_loader_app_exists(const char *root, const char *name);

minishell_platform_result_t adv_elf_loader_apps_list(const char *root,
                                                     minishell_app_emit_fn emit,
                                                     void *ctx);

minishell_platform_result_t adv_elf_loader_app_run(const char *root,
                                                   const char *name,
                                                   int argc,
                                                   char **argv,
                                                   int *out_app_result);

#pragma once

#include "platform_backend.h"

int minishell_app_list(minishell_app_emit_fn emit, void *ctx);
int minishell_app_run(const char *name, int argc, char **argv);

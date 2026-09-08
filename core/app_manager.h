#pragma once

#include "platform_backend.h"

minishell_platform_result_t minishell_app_list(minishell_app_emit_fn emit, void *ctx);
minishell_platform_result_t minishell_app_run(const char *name,
                                              int argc,
                                              char **argv,
                                              int *out_app_result);

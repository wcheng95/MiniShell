#pragma once

#include <stdint.h>

#include "minishell/api.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*minishell_terminal_read_byte_fn)(uint32_t timeout_ms);

void minishell_terminal_backend_init(minishell_terminal_read_byte_fn read_byte);
void minishell_terminal_input_flush(void);

mini_result_t minishell_terminal_display_get_info(void *ctx,
                                                  uint32_t *out_columns,
                                                  uint32_t *out_rows);
mini_result_t minishell_terminal_display_clear(void *ctx);
mini_result_t minishell_terminal_display_clear_at(void *ctx,
                                                  uint32_t row,
                                                  uint32_t column,
                                                  uint32_t rows,
                                                  uint32_t columns);
mini_result_t minishell_terminal_display_write_at(void *ctx,
                                                  uint32_t row,
                                                  uint32_t column,
                                                  const char *text,
                                                  uint32_t byte_count);
mini_result_t minishell_terminal_display_present(void *ctx);

mini_result_t minishell_terminal_input_wait(void *ctx, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

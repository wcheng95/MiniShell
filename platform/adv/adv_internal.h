#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "minishell/api.h"
#include "minishell_services.h"

#ifdef __cplusplus
extern "C" {
#endif

int adv_console_prepare(void);
void adv_console_debug_write(const char *text);

int adv_display_prepare(void);
bool adv_display_ready(void);
void adv_display_console_write(const char *text);
mini_result_t adv_display_text_get_info(void *ctx, uint32_t *out_columns, uint32_t *out_rows);
mini_result_t adv_display_text_clear(void *ctx);
mini_result_t adv_display_text_clear_at(void *ctx, uint32_t row, uint32_t column,
                                        uint32_t rows, uint32_t columns);
mini_result_t adv_display_text_write_at(void *ctx, uint32_t row, uint32_t column,
                                        const char *text, uint32_t byte_count);
mini_result_t adv_display_text_write_at_attr(void *ctx, uint32_t row, uint32_t column,
                                             const char *text, uint32_t byte_count,
                                             uint32_t attributes);
mini_result_t adv_display_present(void *ctx);

int adv_keyboard_prepare(void);
bool adv_keyboard_ready(void);
mini_result_t adv_keyboard_read_event(mini_key_event_t *out_event);
void adv_keyboard_flush(void);

void *adv_memory_alloc(void *ctx, uint32_t size);
void *adv_memory_realloc(void *ctx, void *ptr, uint32_t new_size);
void adv_memory_free(void *ctx, void *ptr);
bool adv_memory_get_info(void *ctx, uint64_t *free_bytes, uint64_t *largest_free_block);

int adv_filesystem_prepare(void);
void adv_filesystem_shutdown(void);
bool adv_filesystem_flash_ready(void);
bool adv_filesystem_sd_ready(void);
void adv_filesystem_configure(minishell_services_port_t *port);
void adv_time_location_configure(minishell_services_port_t *port);

uint64_t adv_monotonic_us(void *ctx);
mini_result_t adv_sleep_ms(void *ctx, uint32_t milliseconds);
mini_result_t adv_input_wait(void *ctx, uint32_t timeout_ms);
void adv_input_flush(void *ctx);

#ifdef __cplusplus
}
#endif

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "minishell/api.h"
#include "nano_buffer.h"

typedef struct {
    const mini_display_api_t *display;
    const mini_text_display_api_t *text;
    uint32_t columns;
    uint32_t rows;
    uint32_t body_rows;
    uint32_t top_line;
    uint32_t left_column;
} nano_ui_t;

bool nano_ui_init(nano_ui_t *ui, const mini_display_api_t *display);
uint32_t nano_ui_body_rows(const nano_ui_t *ui);
bool nano_ui_render(nano_ui_t *ui,
                    const nano_buffer_t *buffer,
                    const char *path,
                    const char *status_text);
void nano_ui_clear(nano_ui_t *ui);

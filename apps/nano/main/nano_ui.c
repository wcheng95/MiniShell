#include "nano_ui.h"

#include <stddef.h>
#include <string.h>

#include "nano_util.h"

#define NANO_UI_LINE_MAX 256u

static uint32_t visible_columns(const nano_ui_t *ui)
{
    return ui->columns < (NANO_UI_LINE_MAX - 1u)
        ? ui->columns
        : (NANO_UI_LINE_MAX - 1u);
}

static bool write_text(const nano_ui_t *ui, uint32_t row, const char *text)
{
    size_t length = strlen(text);
    uint32_t limit = visible_columns(ui);
    if (length > limit) length = limit;
    return ui->text->write_at(row, 0u, text, (uint32_t)length) == MINI_OK;
}

static void update_viewport(nano_ui_t *ui, const nano_buffer_t *buffer)
{
    uint32_t line = 0u;
    uint32_t column = 0u;
    nano_buffer_cursor_line_col(buffer, &line, &column);

    if (line < ui->top_line) {
        ui->top_line = line;
    } else if (line >= ui->top_line + ui->body_rows) {
        ui->top_line = line - ui->body_rows + 1u;
    }

    uint32_t width = visible_columns(ui);
    if (width < 2u) return;
    if (column < ui->left_column) {
        ui->left_column = column;
    } else if (column >= ui->left_column + width - 1u) {
        ui->left_column = column - (width - 2u);
    }
}

static void build_body_line(char *out,
                            uint32_t width,
                            const nano_buffer_t *buffer,
                            uint32_t start,
                            uint32_t end,
                            uint32_t left_column,
                            bool current_line)
{
    uint32_t out_count = 0u;
    uint32_t line_length = end - start;
    uint32_t source_column = left_column < line_length ? left_column : line_length;
    uint32_t position = start + source_column;

    while (out_count < width) {
        if (current_line && position == buffer->cursor) {
            out[out_count++] = '_';
            if (out_count >= width) break;
        }
        if (position >= end) break;

        char ch = buffer->data[position++];
        out[out_count++] = ch == '\t' ? ' ' : ch;
    }
    out[out_count] = '\0';
}

bool nano_ui_init(nano_ui_t *ui, const mini_display_api_t *display)
{
    if (ui == NULL || display == NULL ||
        (display->capabilities & MINI_DISPLAY_CAP_TEXT) == 0u ||
        display->text == NULL || display->present == NULL) {
        return false;
    }

    const mini_text_display_api_t *text = display->text;
    if (text->get_info == NULL || text->clear == NULL || text->write_at == NULL) {
        return false;
    }

    mini_text_display_info_t info;
    info.struct_size = sizeof(mini_text_display_info_t);
    info.columns = 0u;
    info.rows = 0u;
    if (text->get_info(&info) != MINI_OK || info.columns < 20u || info.rows < 5u) {
        return false;
    }

    ui->display = display;
    ui->text = text;
    ui->columns = info.columns;
    ui->rows = info.rows;
    ui->body_rows = info.rows - 3u;
    ui->top_line = 0u;
    ui->left_column = 0u;
    return true;
}

uint32_t nano_ui_body_rows(const nano_ui_t *ui)
{
    return ui == NULL ? 0u : ui->body_rows;
}

bool nano_ui_render(nano_ui_t *ui,
                    const nano_buffer_t *buffer,
                    const char *path,
                    const char *status_text)
{
    if (ui == NULL || buffer == NULL || path == NULL) return false;

    update_viewport(ui, buffer);
    if (ui->text->clear() != MINI_OK) return false;

    char line_buffer[NANO_UI_LINE_MAX];
    nano_string_set(line_buffer, sizeof(line_buffer), "MiniShell nano  ");
    nano_string_append(line_buffer, sizeof(line_buffer), path);
    if (buffer->dirty) nano_string_append(line_buffer, sizeof(line_buffer), " *");
    if (!write_text(ui, 0u, line_buffer)) return false;

    uint32_t cursor_line = 0u;
    uint32_t cursor_column = 0u;
    nano_buffer_cursor_line_col(buffer, &cursor_line, &cursor_column);
    uint32_t width = visible_columns(ui);

    for (uint32_t row = 0u; row < ui->body_rows; ++row) {
        uint32_t line_index = ui->top_line + row;
        uint32_t start = 0u;
        uint32_t end = 0u;
        if (!nano_buffer_line_span(buffer, line_index, &start, &end)) continue;

        build_body_line(line_buffer,
                        width,
                        buffer,
                        start,
                        end,
                        ui->left_column,
                        line_index == cursor_line);
        if (!write_text(ui, row + 1u, line_buffer)) return false;
    }

    if (status_text != NULL && status_text[0] != '\0') {
        if (!write_text(ui, ui->rows - 2u, status_text)) return false;
    } else {
        nano_string_set(line_buffer, sizeof(line_buffer), "Ln ");
        nano_string_append_u32(line_buffer, sizeof(line_buffer), cursor_line + 1u);
        nano_string_append(line_buffer, sizeof(line_buffer), ", Col ");
        nano_string_append_u32(line_buffer, sizeof(line_buffer), cursor_column + 1u);
        if (!write_text(ui, ui->rows - 2u, line_buffer)) return false;
    }

    if (!write_text(ui, ui->rows - 1u, "^O Save   ^W Search   ^X Exit")) return false;
    return ui->display->present() == MINI_OK;
}

void nano_ui_clear(nano_ui_t *ui)
{
    if (ui == NULL || ui->text == NULL || ui->display == NULL) return;
    (void)ui->text->clear();
    (void)ui->display->present();
}

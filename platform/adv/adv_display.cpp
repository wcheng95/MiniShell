#include <cstdint>
#include <cstring>

#include <M5Unified.h>

#include "adv_internal.h"

namespace {
constexpr uint32_t kColumns = 20u;
constexpr uint32_t kRows = 7u;
constexpr int32_t kCellWidth = 12;
constexpr int32_t kRowHeight = 19;
constexpr int32_t kGapY = 19;
constexpr int32_t kGapHeight = 2;
constexpr uint32_t kBlack = 0x000000u;
constexpr uint32_t kWhite = 0xFFFFFFu;

char s_cells[kRows][kColumns];
uint8_t s_attrs[kRows][kColumns];
bool s_ready = false;
bool s_console_mode = true;
uint32_t s_console_row = 0u;
uint32_t s_console_column = 0u;

int32_t row_y(uint32_t row)
{
    return row == 0u ? 0 : 21 + static_cast<int32_t>(row - 1u) * kRowHeight;
}

void clear_buffers(void)
{
    std::memset(s_cells, ' ', sizeof(s_cells));
    std::memset(s_attrs, 0, sizeof(s_attrs));
}

void render_all(void)
{
    if (!s_ready) return;

    auto &display = M5.Display;
    display.fillRect(0, kGapY, 240, kGapHeight, kBlack);
    display.setTextFont(1);
    display.setTextSize(2);

    for (uint32_t row = 0u; row < kRows; ++row) {
        const int32_t y = row_y(row);
        for (uint32_t column = 0u; column < kColumns; ++column) {
            const int32_t x = static_cast<int32_t>(column) * kCellWidth;
            const bool inverse = (s_attrs[row][column] & MINI_TEXT_ATTR_INVERSE) != 0u;
            const uint32_t fg = inverse ? kBlack : kWhite;
            const uint32_t bg = inverse ? kWhite : kBlack;
            display.fillRect(x, y, kCellWidth, kRowHeight, bg);
            display.setTextColor(fg);
            display.setCursor(x, y + 1);
            display.write(static_cast<uint8_t>(s_cells[row][column]));
        }
    }
}

void scroll_console(void)
{
    std::memmove(&s_cells[0][0], &s_cells[1][0], (kRows - 1u) * kColumns);
    std::memmove(&s_attrs[0][0], &s_attrs[1][0], (kRows - 1u) * kColumns);
    std::memset(&s_cells[kRows - 1u][0], ' ', kColumns);
    std::memset(&s_attrs[kRows - 1u][0], 0, kColumns);
    s_console_row = kRows - 1u;
}

void console_newline(void)
{
    s_console_column = 0u;
    ++s_console_row;
    if (s_console_row >= kRows) scroll_console();
}

void ensure_console_mode(void)
{
    if (s_console_mode) return;
    clear_buffers();
    s_console_row = 0u;
    s_console_column = 0u;
    s_console_mode = true;
}

char display_char(uint8_t ch)
{
    return (ch >= 0x20u && ch <= 0x7eu) ? static_cast<char>(ch) : '?';
}
}

extern "C" int adv_display_prepare(void)
{
    M5.Display.begin();
    M5.Display.setRotation(1);
    if (M5.Display.width() != 240 || M5.Display.height() != 135) return -1;

    M5.Display.setTextFont(1);
    M5.Display.setTextSize(2);
    M5.Display.setTextWrap(false);
    M5.Display.fillScreen(kBlack);

    clear_buffers();
    s_console_row = 0u;
    s_console_column = 0u;
    s_console_mode = true;
    s_ready = true;
    render_all();
    return 0;
}

extern "C" bool adv_display_ready(void)
{
    return s_ready;
}

extern "C" void adv_display_console_write(const char *text)
{
    if (!s_ready || text == nullptr) return;
    ensure_console_mode();

    for (const unsigned char *p = reinterpret_cast<const unsigned char *>(text); *p != 0u; ++p) {
        const unsigned char ch = *p;
        if (ch == '\r') continue;
        if (ch == '\n') {
            console_newline();
            continue;
        }
        if (ch == '\b') {
            if (s_console_column > 0u) {
                --s_console_column;
                s_cells[s_console_row][s_console_column] = ' ';
                s_attrs[s_console_row][s_console_column] = 0u;
            }
            continue;
        }
        if (ch < 0x20u || ch > 0x7eu) continue;

        s_cells[s_console_row][s_console_column] = static_cast<char>(ch);
        s_attrs[s_console_row][s_console_column] = 0u;
        ++s_console_column;
        if (s_console_column >= kColumns) console_newline();
    }
    render_all();
}

extern "C" mini_result_t adv_display_text_get_info(void *ctx, uint32_t *out_columns,
                                                     uint32_t *out_rows)
{
    (void)ctx;
    if (!s_ready) return MINI_ERR_NOT_READY;
    if (out_columns == nullptr || out_rows == nullptr) return MINI_ERR_INVALID;
    *out_columns = kColumns;
    *out_rows = kRows;
    return MINI_OK;
}

extern "C" mini_result_t adv_display_text_clear(void *ctx)
{
    (void)ctx;
    if (!s_ready) return MINI_ERR_NOT_READY;
    clear_buffers();
    s_console_mode = false;
    return MINI_OK;
}

extern "C" mini_result_t adv_display_text_clear_at(void *ctx, uint32_t row, uint32_t column,
                                                    uint32_t rows, uint32_t columns)
{
    (void)ctx;
    if (!s_ready) return MINI_ERR_NOT_READY;
    for (uint32_t r = 0u; r < rows; ++r) {
        for (uint32_t c = 0u; c < columns; ++c) {
            s_cells[row + r][column + c] = ' ';
            s_attrs[row + r][column + c] = 0u;
        }
    }
    s_console_mode = false;
    return MINI_OK;
}

extern "C" mini_result_t adv_display_text_write_at(void *ctx, uint32_t row, uint32_t column,
                                                     const char *text, uint32_t byte_count)
{
    (void)ctx;
    if (!s_ready) return MINI_ERR_NOT_READY;
    if (text == nullptr && byte_count != 0u) return MINI_ERR_INVALID;
    for (uint32_t i = 0u; i < byte_count; ++i) {
        s_cells[row][column + i] = display_char(static_cast<uint8_t>(text[i]));
        s_attrs[row][column + i] = 0u;
    }
    s_console_mode = false;
    return MINI_OK;
}

extern "C" mini_result_t adv_display_text_write_at_attr(void *ctx, uint32_t row, uint32_t column,
                                                          const char *text, uint32_t byte_count,
                                                          uint32_t attributes)
{
    (void)ctx;
    if (!s_ready) return MINI_ERR_NOT_READY;
    if (text == nullptr && byte_count != 0u) return MINI_ERR_INVALID;
    for (uint32_t i = 0u; i < byte_count; ++i) {
        s_cells[row][column + i] = display_char(static_cast<uint8_t>(text[i]));
        s_attrs[row][column + i] = static_cast<uint8_t>(attributes);
    }
    s_console_mode = false;
    return MINI_OK;
}

extern "C" mini_result_t adv_display_present(void *ctx)
{
    (void)ctx;
    if (!s_ready) return MINI_ERR_NOT_READY;
    render_all();
    return MINI_OK;
}

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
uint32_t s_separator = MINI_TEXT_ATTR_FG_DEFAULT;
bool s_ready = false;
bool s_console_mode = true;
constexpr uint32_t kHistoryRows = 50u;
char s_history[kHistoryRows][kColumns];
uint32_t s_history_first = 0u;
uint32_t s_history_count = 1u;
uint32_t s_console_offset = 0u;
uint32_t s_console_column = 0u;
// Snapshot the committed prompt tail while editing. Restoring it also recovers
// rows temporarily displaced by a longer draft, without growing scrollback.
char s_edit_history[kHistoryRows][kColumns];
uint32_t s_edit_first, s_edit_count, s_edit_column;
bool s_edit_active = false, s_edit_visible = false, s_edit_scrollback = false;
uint32_t s_edit_cursor_row, s_edit_cursor_column;

uint32_t console_max_offset(void);

bool cursor_cell(uint32_t *row)
{
    if (!s_edit_active || !s_console_mode || s_edit_scrollback) return false;
    const uint32_t start = console_max_offset() - s_console_offset;
    if (s_edit_cursor_row < start || s_edit_cursor_row >= start + kRows) return false;
    *row = s_edit_cursor_row - start;
    return true;
}

int32_t row_y(uint32_t row)
{
    return row == 0u ? 0 : 21 + static_cast<int32_t>(row - 1u) * kRowHeight;
}

uint32_t foreground(uint32_t attr)
{
    switch (attr & MINI_TEXT_ATTR_FG_MASK) {
    case MINI_TEXT_ATTR_FG_GREEN: return 0x00FF00u;
    case MINI_TEXT_ATTR_FG_CYAN: return 0x00FFFFu;
    case MINI_TEXT_ATTR_FG_RED: return 0xFF0000u;
    default: return kWhite;
    }
}

void clear_buffers(void)
{
    s_separator = MINI_TEXT_ATTR_FG_DEFAULT;
    std::memset(s_cells, ' ', sizeof(s_cells));
    std::memset(s_attrs, 0, sizeof(s_attrs));
}

void render_cell(uint32_t row, uint32_t column)
{
    auto &display = M5.Display;
    const int32_t x = static_cast<int32_t>(column) * kCellWidth;
    const int32_t y = row_y(row);
    uint32_t cursor_row;
    const bool overlay = s_edit_visible && cursor_cell(&cursor_row) &&
                         row == cursor_row && column == s_edit_cursor_column;
    const bool inverse = ((s_attrs[row][column] & MINI_TEXT_ATTR_INVERSE) != 0u) != overlay;
    const uint32_t fg = inverse ? kBlack : foreground(s_attrs[row][column]);
    const uint32_t bg = inverse ? foreground(s_attrs[row][column]) : kBlack;
    display.fillRect(x, y, kCellWidth, kRowHeight, bg);
    display.setTextColor(fg);
    display.setCursor(x, y + 1);
    display.write(static_cast<uint8_t>(s_cells[row][column]));
}

void render_all(void)
{
    if (!s_ready) return;
    auto &display = M5.Display;
    display.fillRect(0, kGapY, 240, kGapHeight, s_separator ? foreground(s_separator) : kBlack);
    display.setTextFont(1);
    display.setTextSize(2);
    for (uint32_t row = 0u; row < kRows; ++row)
        for (uint32_t column = 0u; column < kColumns; ++column)
            render_cell(row, column);
}

uint32_t console_tail(void)
{
    return (s_history_first + s_history_count - 1u) % kHistoryRows;
}

void console_newline(void)
{
    s_console_column = 0u;
    if (s_history_count < kHistoryRows) {
        ++s_history_count;
    } else {
        s_history_first = (s_history_first + 1u) % kHistoryRows;
    }
    std::memset(s_history[console_tail()], ' ', kColumns);
}

uint32_t console_max_offset(void)
{
    return s_history_count > kRows ? s_history_count - kRows : 0u;
}

void restore_console_view(void)
{
    // App Display buffers never own the retained console rows.
    clear_buffers();
    const uint32_t start = console_max_offset() - s_console_offset;
    for (uint32_t row = 0u; row < kRows && start + row < s_history_count; ++row) {
        const uint32_t index = (s_history_first + start + row) % kHistoryRows;
        std::memcpy(s_cells[row], s_history[index], kColumns);
    }
    s_console_mode = true;
}

void console_append(const char *text)
{
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
                s_history[console_tail()][s_console_column] = ' ';
            }
            continue;
        }
        if (ch < 0x20u || ch > 0x7eu) continue;

        s_history[console_tail()][s_console_column] = static_cast<char>(ch);
        ++s_console_column;
        if (s_console_column >= kColumns) console_newline();
    }
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
    std::memset(s_history, ' ', sizeof(s_history));
    s_history_first = 0u;
    s_history_count = 1u;
    s_console_offset = 0u;
    s_console_column = 0u;
    s_console_mode = true;
    s_edit_active = s_edit_visible = s_edit_scrollback = false;
    s_ready = true;
    render_all();
    return 0;
}

extern "C" bool adv_display_ready(void)
{
    return s_ready;
}

extern "C" void adv_display_console_clear(void)
{
    clear_buffers();
    std::memset(s_history, ' ', sizeof(s_history));
    std::memset(s_edit_history, ' ', sizeof(s_edit_history));
    s_history_first = s_console_offset = s_console_column = 0u;
    s_history_count = 1u;
    s_edit_first = s_edit_column = s_edit_cursor_row = s_edit_cursor_column = 0u;
    s_edit_count = 1u;
    s_console_mode = true;
    s_edit_active = s_edit_visible = s_edit_scrollback = false;
    render_all();
}

extern "C" void adv_display_console_write(const char *text)
{
    if (!s_ready || text == nullptr) return;
    s_console_offset = 0u;
    s_edit_active = false;

    console_append(text);
    restore_console_view();
    render_all();
}

extern "C" void adv_display_console_edit_begin(void)
{
    std::memcpy(s_edit_history, s_history, sizeof(s_history));
    s_edit_first = s_history_first;
    s_edit_count = s_history_count;
    s_edit_column = s_console_column;
    s_edit_cursor_row = s_history_count - 1u;
    s_edit_cursor_column = s_console_column;
    s_edit_active = s_edit_visible = true;
    s_edit_scrollback = false;
    s_console_offset = 0u;
    restore_console_view();
    render_all();
}

extern "C" void adv_display_console_edit_discard(void)
{
    // Remove the draft before ordinary output, recovering rows it displaced.
    s_edit_active = s_edit_visible = false;
    std::memcpy(s_history, s_edit_history, sizeof(s_history));
    s_history_first = s_edit_first;
    s_history_count = s_edit_count;
    s_console_column = s_edit_column;
    s_console_offset = 0u;
    restore_console_view();
    render_all();
}

extern "C" void adv_display_console_edit_line(const char *line, size_t cursor)
{
    const size_t length = std::strlen(line);
    if (cursor > length) cursor = length;
    uint32_t offset = s_edit_scrollback ? 0u : s_console_offset;
    std::memcpy(s_history, s_edit_history, sizeof(s_history));
    s_history_first = s_edit_first;
    s_history_count = s_edit_count;
    s_console_column = s_edit_column;
    console_append(line);

    // Cursor and viewport rows are relative to the retained ring's oldest row.
    // Appending the draft may have temporarily displaced snapshot rows.
    const size_t rows = s_edit_count + (s_edit_column + length) / kColumns;
    const size_t evicted = rows > kHistoryRows ? rows - kHistoryRows : 0u;
    s_edit_cursor_row = s_edit_count - 1u + (s_edit_column + cursor) / kColumns - evicted;
    s_edit_cursor_column = (s_edit_column + cursor) % kColumns;
    const uint32_t maximum = console_max_offset();
    if (offset > maximum) offset = maximum;
    uint32_t start = maximum - offset;
    if (s_edit_cursor_row < start) start = s_edit_cursor_row;
    else if (s_edit_cursor_row >= start + kRows) start = s_edit_cursor_row - kRows + 1u;
    s_console_offset = maximum - start;
    s_edit_active = s_edit_visible = true;
    s_edit_scrollback = false;
    restore_console_view();
    render_all();
}

extern "C" void adv_display_console_edit_cursor(bool visible)
{
    if (!s_ready || !s_edit_active || s_edit_visible == visible) return;
    s_edit_visible = visible;
    uint32_t row;
    if (cursor_cell(&row)) render_cell(row, s_edit_cursor_column);
}

extern "C" void adv_display_console_edit_end(void)
{
    adv_display_console_edit_cursor(false);
    s_edit_active = false;
}

extern "C" void adv_display_console_scroll(int delta)
{
    if (!s_ready || !s_console_mode) return;
    const int64_t offset = static_cast<int64_t>(s_console_offset) + delta;
    const uint32_t maximum = console_max_offset();
    s_console_offset = offset < 0 ? 0u :
                       offset > maximum ? maximum : static_cast<uint32_t>(offset);
    s_edit_scrollback = s_console_offset != 0u;
    restore_console_view();
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

extern "C" mini_result_t adv_display_set_row_separator(void *ctx, uint32_t row, uint32_t color)
{
    (void)ctx;
    if (!s_ready) return MINI_ERR_NOT_READY;
    if ((color & ~MINI_TEXT_ATTR_FG_MASK) || color > MINI_TEXT_ATTR_FG_RED) return MINI_ERR_INVALID;
    if (row != 0u) return MINI_ERR_UNSUPPORTED;
    s_separator = color;
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

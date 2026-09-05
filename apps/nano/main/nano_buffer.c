#include "nano_buffer.h"

#include <stddef.h>
#include <string.h>

#define NANO_INITIAL_CAPACITY 128u

static void reset_goal(nano_buffer_t *buffer)
{
    buffer->goal_valid = false;
}

static bool reserve(nano_buffer_t *buffer, uint32_t needed)
{
    if (needed <= buffer->capacity) return true;
    if (needed > NANO_MAX_BYTES) return false;

    uint32_t capacity = buffer->capacity == 0u ? NANO_INITIAL_CAPACITY : buffer->capacity;
    while (capacity < needed) {
        if (capacity >= NANO_MAX_BYTES / 2u) {
            capacity = NANO_MAX_BYTES;
            break;
        }
        capacity *= 2u;
    }

    void *new_data = buffer->data == NULL
        ? buffer->allocator.alloc(buffer->allocator.ctx, capacity)
        : buffer->allocator.realloc(buffer->allocator.ctx, buffer->data, capacity);
    if (new_data == NULL) return false;

    buffer->data = (char *)new_data;
    buffer->capacity = capacity;
    return true;
}

static uint32_t line_start_for_position(const nano_buffer_t *buffer, uint32_t position)
{
    if (position > buffer->length) position = buffer->length;
    while (position > 0u && buffer->data[position - 1u] != '\n') {
        --position;
    }
    return position;
}

static uint32_t line_end_for_position(const nano_buffer_t *buffer, uint32_t position)
{
    if (position > buffer->length) position = buffer->length;
    while (position < buffer->length && buffer->data[position] != '\n') {
        ++position;
    }
    return position;
}

static uint32_t current_column(const nano_buffer_t *buffer)
{
    return buffer->cursor - line_start_for_position(buffer, buffer->cursor);
}

static uint32_t target_in_line(const nano_buffer_t *buffer,
                               uint32_t line_start,
                               uint32_t column)
{
    uint32_t line_end = line_end_for_position(buffer, line_start);
    uint32_t line_length = line_end - line_start;
    return line_start + (column < line_length ? column : line_length);
}

bool nano_buffer_init(nano_buffer_t *buffer, const nano_allocator_t *allocator)
{
    if (buffer == NULL || allocator == NULL ||
        allocator->alloc == NULL || allocator->realloc == NULL || allocator->free == NULL) {
        return false;
    }

    memset(buffer, 0, sizeof(*buffer));
    buffer->allocator = *allocator;
    return true;
}

void nano_buffer_destroy(nano_buffer_t *buffer)
{
    if (buffer == NULL) return;
    if (buffer->data != NULL) {
        buffer->allocator.free(buffer->allocator.ctx, buffer->data);
    }
    memset(buffer, 0, sizeof(*buffer));
}

bool nano_buffer_assign(nano_buffer_t *buffer, const char *data, uint32_t length)
{
    if (buffer == NULL || (data == NULL && length != 0u) || length > NANO_MAX_BYTES) {
        return false;
    }
    if (length != 0u && !reserve(buffer, length)) return false;

    if (length != 0u) memcpy(buffer->data, data, length);
    buffer->length = length;
    buffer->cursor = 0u;
    buffer->dirty = false;
    reset_goal(buffer);
    return true;
}

void nano_buffer_mark_clean(nano_buffer_t *buffer)
{
    if (buffer != NULL) buffer->dirty = false;
}

bool nano_buffer_insert_char(nano_buffer_t *buffer, char ch)
{
    if (buffer == NULL || buffer->length >= NANO_MAX_BYTES) return false;
    if (!reserve(buffer, buffer->length + 1u)) return false;

    memmove(buffer->data + buffer->cursor + 1u,
            buffer->data + buffer->cursor,
            buffer->length - buffer->cursor);
    buffer->data[buffer->cursor] = ch;
    ++buffer->cursor;
    ++buffer->length;
    buffer->dirty = true;
    reset_goal(buffer);
    return true;
}

bool nano_buffer_insert_spaces(nano_buffer_t *buffer, uint32_t count)
{
    if (buffer == NULL) return false;
    if (count > NANO_MAX_BYTES - buffer->length) return false;
    if (count == 0u) return true;
    if (!reserve(buffer, buffer->length + count)) return false;

    memmove(buffer->data + buffer->cursor + count,
            buffer->data + buffer->cursor,
            buffer->length - buffer->cursor);
    memset(buffer->data + buffer->cursor, ' ', count);
    buffer->cursor += count;
    buffer->length += count;
    buffer->dirty = true;
    reset_goal(buffer);
    return true;
}

bool nano_buffer_backspace(nano_buffer_t *buffer)
{
    if (buffer == NULL || buffer->cursor == 0u) return false;

    memmove(buffer->data + buffer->cursor - 1u,
            buffer->data + buffer->cursor,
            buffer->length - buffer->cursor);
    --buffer->cursor;
    --buffer->length;
    buffer->dirty = true;
    reset_goal(buffer);
    return true;
}

bool nano_buffer_delete(nano_buffer_t *buffer)
{
    if (buffer == NULL || buffer->cursor >= buffer->length) return false;

    memmove(buffer->data + buffer->cursor,
            buffer->data + buffer->cursor + 1u,
            buffer->length - buffer->cursor - 1u);
    --buffer->length;
    buffer->dirty = true;
    reset_goal(buffer);
    return true;
}

void nano_buffer_move_left(nano_buffer_t *buffer)
{
    if (buffer == NULL) return;
    if (buffer->cursor > 0u) --buffer->cursor;
    reset_goal(buffer);
}

void nano_buffer_move_right(nano_buffer_t *buffer)
{
    if (buffer == NULL) return;
    if (buffer->cursor < buffer->length) ++buffer->cursor;
    reset_goal(buffer);
}

void nano_buffer_move_home(nano_buffer_t *buffer)
{
    if (buffer == NULL) return;
    buffer->cursor = line_start_for_position(buffer, buffer->cursor);
    reset_goal(buffer);
}

void nano_buffer_move_end(nano_buffer_t *buffer)
{
    if (buffer == NULL) return;
    buffer->cursor = line_end_for_position(buffer, buffer->cursor);
    reset_goal(buffer);
}

void nano_buffer_move_up(nano_buffer_t *buffer)
{
    if (buffer == NULL) return;

    uint32_t start = line_start_for_position(buffer, buffer->cursor);
    if (start == 0u) return;

    if (!buffer->goal_valid) {
        buffer->goal_column = current_column(buffer);
        buffer->goal_valid = true;
    }

    uint32_t previous_end = start - 1u;
    uint32_t previous_start = line_start_for_position(buffer, previous_end);
    buffer->cursor = target_in_line(buffer, previous_start, buffer->goal_column);
}

void nano_buffer_move_down(nano_buffer_t *buffer)
{
    if (buffer == NULL) return;

    uint32_t end = line_end_for_position(buffer, buffer->cursor);
    if (end >= buffer->length) return;

    if (!buffer->goal_valid) {
        buffer->goal_column = current_column(buffer);
        buffer->goal_valid = true;
    }

    uint32_t next_start = end + 1u;
    buffer->cursor = target_in_line(buffer, next_start, buffer->goal_column);
}

void nano_buffer_page_up(nano_buffer_t *buffer, uint32_t rows)
{
    if (buffer == NULL) return;
    for (uint32_t i = 0u; i < rows; ++i) {
        uint32_t before = buffer->cursor;
        nano_buffer_move_up(buffer);
        if (buffer->cursor == before) break;
    }
}

void nano_buffer_page_down(nano_buffer_t *buffer, uint32_t rows)
{
    if (buffer == NULL) return;
    for (uint32_t i = 0u; i < rows; ++i) {
        uint32_t before = buffer->cursor;
        nano_buffer_move_down(buffer);
        if (buffer->cursor == before) break;
    }
}

void nano_buffer_cursor_line_col(const nano_buffer_t *buffer,
                                 uint32_t *out_line,
                                 uint32_t *out_column)
{
    if (buffer == NULL) return;

    uint32_t line = 0u;
    uint32_t line_start = 0u;
    for (uint32_t i = 0u; i < buffer->cursor && i < buffer->length; ++i) {
        if (buffer->data[i] == '\n') {
            ++line;
            line_start = i + 1u;
        }
    }

    if (out_line != NULL) *out_line = line;
    if (out_column != NULL) *out_column = buffer->cursor - line_start;
}

uint32_t nano_buffer_line_count(const nano_buffer_t *buffer)
{
    if (buffer == NULL) return 0u;
    uint32_t lines = 1u;
    for (uint32_t i = 0u; i < buffer->length; ++i) {
        if (buffer->data[i] == '\n') ++lines;
    }
    return lines;
}

bool nano_buffer_line_span(const nano_buffer_t *buffer,
                           uint32_t line,
                           uint32_t *out_start,
                           uint32_t *out_end)
{
    if (buffer == NULL || out_start == NULL || out_end == NULL) return false;

    uint32_t current_line = 0u;
    uint32_t start = 0u;
    for (uint32_t i = 0u; i <= buffer->length; ++i) {
        bool at_end = i == buffer->length;
        bool at_newline = !at_end && buffer->data[i] == '\n';
        if ((at_newline || at_end) && current_line == line) {
            *out_start = start;
            *out_end = i;
            return true;
        }
        if (at_newline) {
            ++current_line;
            start = i + 1u;
        }
    }
    return false;
}

static bool match_at(const nano_buffer_t *buffer,
                     const char *needle,
                     uint32_t needle_length,
                     uint32_t position)
{
    if (needle_length > buffer->length - position) return false;
    return memcmp(buffer->data + position, needle, needle_length) == 0;
}

bool nano_buffer_search_forward(const nano_buffer_t *buffer,
                                const char *needle,
                                uint32_t needle_length,
                                uint32_t *out_position)
{
    if (buffer == NULL || needle == NULL || out_position == NULL ||
        needle_length == 0u || needle_length > buffer->length) {
        return false;
    }

    uint32_t start = buffer->cursor < buffer->length ? buffer->cursor + 1u : 0u;

    for (uint32_t i = start; i + needle_length <= buffer->length; ++i) {
        if (match_at(buffer, needle, needle_length, i)) {
            *out_position = i;
            return true;
        }
    }

    for (uint32_t i = 0u; i < start && i + needle_length <= buffer->length; ++i) {
        if (match_at(buffer, needle, needle_length, i)) {
            *out_position = i;
            return true;
        }
    }
    return false;
}

void nano_buffer_set_cursor(nano_buffer_t *buffer, uint32_t position)
{
    if (buffer == NULL) return;
    buffer->cursor = position <= buffer->length ? position : buffer->length;
    reset_goal(buffer);
}

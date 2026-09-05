#pragma once

#include <stdbool.h>
#include <stdint.h>

#define NANO_MAX_BYTES (64u * 1024u)

typedef void *(*nano_alloc_fn)(void *ctx, uint32_t size);
typedef void *(*nano_realloc_fn)(void *ctx, void *ptr, uint32_t size);
typedef void (*nano_free_fn)(void *ctx, void *ptr);

typedef struct {
    void *ctx;
    nano_alloc_fn alloc;
    nano_realloc_fn realloc;
    nano_free_fn free;
} nano_allocator_t;

typedef struct {
    char *data;
    uint32_t length;
    uint32_t capacity;
    uint32_t cursor;
    uint32_t goal_column;
    bool goal_valid;
    bool dirty;
    nano_allocator_t allocator;
} nano_buffer_t;

bool nano_buffer_init(nano_buffer_t *buffer, const nano_allocator_t *allocator);
void nano_buffer_destroy(nano_buffer_t *buffer);

bool nano_buffer_assign(nano_buffer_t *buffer, const char *data, uint32_t length);
void nano_buffer_mark_clean(nano_buffer_t *buffer);

bool nano_buffer_insert_char(nano_buffer_t *buffer, char ch);
bool nano_buffer_insert_spaces(nano_buffer_t *buffer, uint32_t count);
bool nano_buffer_backspace(nano_buffer_t *buffer);
bool nano_buffer_delete(nano_buffer_t *buffer);

void nano_buffer_move_left(nano_buffer_t *buffer);
void nano_buffer_move_right(nano_buffer_t *buffer);
void nano_buffer_move_home(nano_buffer_t *buffer);
void nano_buffer_move_end(nano_buffer_t *buffer);
void nano_buffer_move_up(nano_buffer_t *buffer);
void nano_buffer_move_down(nano_buffer_t *buffer);
void nano_buffer_page_up(nano_buffer_t *buffer, uint32_t rows);
void nano_buffer_page_down(nano_buffer_t *buffer, uint32_t rows);

void nano_buffer_cursor_line_col(const nano_buffer_t *buffer,
                                 uint32_t *out_line,
                                 uint32_t *out_column);
uint32_t nano_buffer_line_count(const nano_buffer_t *buffer);
bool nano_buffer_line_span(const nano_buffer_t *buffer,
                           uint32_t line,
                           uint32_t *out_start,
                           uint32_t *out_end);

bool nano_buffer_search_forward(const nano_buffer_t *buffer,
                                const char *needle,
                                uint32_t needle_length,
                                uint32_t *out_position);
void nano_buffer_set_cursor(nano_buffer_t *buffer, uint32_t position);

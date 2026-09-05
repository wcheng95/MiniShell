#include "test_support.h"
#include "nano_buffer.h"

static void *host_alloc(void *ctx, uint32_t size)
{
    (void)ctx;
    return malloc(size);
}

static void *host_realloc(void *ctx, void *ptr, uint32_t size)
{
    (void)ctx;
    return realloc(ptr, size);
}

static void host_free(void *ctx, void *ptr)
{
    (void)ctx;
    free(ptr);
}

bool test_nano_buffer(void)
{
    const nano_allocator_t allocator = {
        .ctx = NULL,
        .alloc = host_alloc,
        .realloc = host_realloc,
        .free = host_free,
    };
    nano_buffer_t buffer;

    TEST_CHECK(nano_buffer_init(&buffer, &allocator));
    TEST_EQ(buffer.length, 0u);
    TEST_EQ(buffer.cursor, 0u);
    TEST_CHECK(!buffer.dirty);
    TEST_EQ(nano_buffer_line_count(&buffer), 1u);

    TEST_CHECK(nano_buffer_insert_char(&buffer, 'a'));
    TEST_CHECK(nano_buffer_insert_char(&buffer, 'b'));
    TEST_CHECK(nano_buffer_insert_char(&buffer, 'c'));
    TEST_EQ(buffer.length, 3u);
    TEST_EQ(buffer.cursor, 3u);
    TEST_CHECK(buffer.dirty);
    TEST_CHECK(memcmp(buffer.data, "abc", 3u) == 0);

    nano_buffer_move_left(&buffer);
    TEST_EQ(buffer.cursor, 2u);
    TEST_CHECK(nano_buffer_insert_char(&buffer, 'X'));
    TEST_CHECK(memcmp(buffer.data, "abXc", 4u) == 0);
    TEST_CHECK(nano_buffer_backspace(&buffer));
    TEST_CHECK(memcmp(buffer.data, "abc", 3u) == 0);
    TEST_CHECK(nano_buffer_delete(&buffer));
    TEST_CHECK(memcmp(buffer.data, "ab", 2u) == 0);

    TEST_CHECK(nano_buffer_assign(&buffer, "12345\nx\nABCDE", 13u));
    TEST_CHECK(!buffer.dirty);
    TEST_EQ(nano_buffer_line_count(&buffer), 3u);

    nano_buffer_set_cursor(&buffer, 5u);
    nano_buffer_move_down(&buffer);
    TEST_EQ(buffer.cursor, 7u);
    nano_buffer_move_down(&buffer);
    TEST_EQ(buffer.cursor, 13u);
    nano_buffer_move_up(&buffer);
    TEST_EQ(buffer.cursor, 7u);

    nano_buffer_move_home(&buffer);
    TEST_EQ(buffer.cursor, 6u);
    nano_buffer_move_end(&buffer);
    TEST_EQ(buffer.cursor, 7u);

    uint32_t line = 0u;
    uint32_t column = 0u;
    nano_buffer_cursor_line_col(&buffer, &line, &column);
    TEST_EQ(line, 1u);
    TEST_EQ(column, 1u);

    uint32_t start = 0u;
    uint32_t end = 0u;
    TEST_CHECK(nano_buffer_line_span(&buffer, 2u, &start, &end));
    TEST_EQ(start, 8u);
    TEST_EQ(end, 13u);

    TEST_CHECK(nano_buffer_assign(&buffer, "one two one", 11u));
    uint32_t found = 0u;
    nano_buffer_set_cursor(&buffer, 0u);
    TEST_CHECK(nano_buffer_search_forward(&buffer, "one", 3u, &found));
    TEST_EQ(found, 8u);
    nano_buffer_set_cursor(&buffer, 8u);
    TEST_CHECK(nano_buffer_search_forward(&buffer, "one", 3u, &found));
    TEST_EQ(found, 0u);
    TEST_CHECK(!nano_buffer_search_forward(&buffer, "xyz", 3u, &found));

    TEST_CHECK(nano_buffer_assign(&buffer, NULL, 0u));
    for (uint32_t i = 0u; i < 1000u; ++i) {
        TEST_CHECK(nano_buffer_insert_char(&buffer, (char)('a' + (i % 26u))));
    }
    TEST_EQ(buffer.length, 1000u);
    TEST_EQ(buffer.cursor, 1000u);
    TEST_CHECK(buffer.capacity >= 1000u);
    TEST_CHECK(buffer.dirty);

    nano_buffer_mark_clean(&buffer);
    TEST_CHECK(!buffer.dirty);
    TEST_CHECK(nano_buffer_insert_spaces(&buffer, 4u));
    TEST_EQ(buffer.length, 1004u);
    TEST_CHECK(buffer.dirty);

    nano_buffer_destroy(&buffer);
    TEST_EQ(buffer.data, NULL);
    TEST_EQ(buffer.length, 0u);
    return true;
}

#include "test_support.h"

static uint32_t captured_attr, captured_separator;
static mini_result_t attr_write(void *ctx, uint32_t r, uint32_t c, const char *s, uint32_t n, uint32_t attr)
{
    (void)ctx; (void)r; (void)c; (void)s; (void)n; captured_attr = attr; return MINI_OK;
}
static mini_result_t separator(void *ctx, uint32_t row, uint32_t color)
{
    (void)ctx; if (row != 0) return MINI_ERR_UNSUPPORTED;
    captured_separator = color; return MINI_OK;
}
bool test_display(void)
{
    TEST_EQ(MINI_TEXT_ATTR_FG_DEFAULT, 0u);
    TEST_EQ(MINI_TEXT_ATTR_FG_WHITE, 2u);
    TEST_EQ(MINI_TEXT_ATTR_FG_GREEN, 4u);
    TEST_EQ(MINI_TEXT_ATTR_FG_CYAN, 6u);
    TEST_EQ(MINI_TEXT_ATTR_FG_RED, 8u);
    fake_reset();
    minishell_services_port_t p = fake_full_port();
    minishell_services_configure(&p);

    const mini_display_api_t *display = mini_api_get()->display;
    TEST_CHECK(display != NULL);
    TEST_CHECK((display->capabilities & MINI_DISPLAY_CAP_TEXT) != 0u);
    TEST_CHECK(display->text != NULL);

    mini_text_display_info_t info = {.struct_size = sizeof(info)};
    TEST_EQ(display->text->get_info(&info), MINI_OK);
    TEST_EQ(info.columns, 10u);
    TEST_EQ(info.rows, 4u);

    TEST_EQ(display->text->clear(), MINI_OK);
    TEST_EQ(g_fake.display_clear_calls, 1u);
    TEST_EQ(display->text->write_at(0, 0, "hello", 5), MINI_OK);
    TEST_EQ(g_fake.display_last_write_count, 5u);
    TEST_CHECK(memcmp(g_fake.display_cells[0], "hello", 5) == 0);
    TEST_EQ(display->text->write_at(1, 8, "abcdef", 6), MINI_OK);
    TEST_EQ(g_fake.display_last_write_count, 2u);
    TEST_CHECK(g_fake.display_cells[1][8] == 'a');
    TEST_CHECK(g_fake.display_cells[1][9] == 'b');
    TEST_EQ(display->text->write_at(4, 0, "x", 1), MINI_ERR_INVALID);
    TEST_EQ(display->text->write_at(0, 10, "x", 1), MINI_ERR_INVALID);
    TEST_EQ(display->text->write_at(0, 0, NULL, 1), MINI_ERR_INVALID);
    TEST_EQ(display->text->write_at(0, 0, NULL, 0), MINI_OK);
    TEST_EQ(display->text->clear_at(2, 7, 9, 9), MINI_OK);
    TEST_EQ(g_fake.display_last_clear_row, 2u);
    TEST_EQ(g_fake.display_last_clear_col, 7u);
    TEST_EQ(g_fake.display_last_clear_rows, 2u);
    TEST_EQ(g_fake.display_last_clear_cols, 3u);
    TEST_EQ(display->text->clear_at(0, 0, 0, 4), MINI_OK);
    TEST_EQ(display->text->clear_at(4, 0, 0, 0), MINI_ERR_INVALID);
    TEST_EQ(display->present(), MINI_OK);
    TEST_EQ(g_fake.display_present_calls, 1u);
    mini_text_display_info_t small = {.struct_size = sizeof(uint32_t)};
    TEST_EQ(display->text->get_info(&small), MINI_ERR_INVALID);

    p.display_text_write_at_attr = attr_write;
    p.display_set_row_separator = separator;
    p.display_capabilities |= MINI_DISPLAY_CAP_TEXT_COLOR | MINI_DISPLAY_CAP_ROW_SEPARATOR;
    minishell_services_configure(&p);
    TEST_CHECK(display->capabilities & MINI_DISPLAY_CAP_TEXT_COLOR);
    TEST_CHECK(display->capabilities & MINI_DISPLAY_CAP_ROW_SEPARATOR);
    const uint32_t colors[] = {MINI_TEXT_ATTR_FG_WHITE, MINI_TEXT_ATTR_FG_GREEN, MINI_TEXT_ATTR_FG_CYAN, MINI_TEXT_ATTR_FG_RED};
    for (unsigned i=0;i<4;++i) {
        uint32_t attr = colors[i] | MINI_TEXT_ATTR_INVERSE;
        TEST_EQ(display->text->write_at_attr(0,0,"A",1,attr), MINI_OK);
        TEST_EQ(captured_attr, attr);
        TEST_EQ(display->text->set_row_separator(0,colors[i]), MINI_OK);
        TEST_EQ(captured_separator, colors[i]);
    }
    TEST_EQ(display->text->write_at_attr(0,0,"A",1,16), MINI_ERR_INVALID);
    TEST_EQ(display->text->write_at_attr(0,0,"A",1,5u<<1), MINI_ERR_INVALID);
    TEST_EQ(display->text->set_row_separator(0,5u<<1), MINI_ERR_INVALID);
    TEST_EQ(display->text->set_row_separator(0,MINI_TEXT_ATTR_INVERSE), MINI_ERR_INVALID);
    TEST_EQ(display->text->set_row_separator(4,0), MINI_ERR_INVALID);
    TEST_EQ(display->text->set_row_separator(1,0), MINI_ERR_UNSUPPORTED);
    p.display_capabilities = MINI_DISPLAY_CAP_TEXT;
    minishell_services_configure(&p);
    TEST_EQ(display->text->write_at_attr(0,0,"A",1,MINI_TEXT_ATTR_FG_GREEN), MINI_ERR_UNSUPPORTED);
    TEST_EQ(display->text->set_row_separator(0,MINI_TEXT_ATTR_FG_GREEN), MINI_ERR_UNSUPPORTED);
    TEST_EQ(display->text->write_at_attr(0,0,"A",1,MINI_TEXT_ATTR_INVERSE), MINI_OK);
    fake_reset();
    minishell_services_port_t no_display = fake_full_port();
    no_display.display_present = NULL;
    minishell_services_configure(&no_display);
    TEST_CHECK(mini_api_get()->display == NULL);
    return true;
}

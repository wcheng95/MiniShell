#include "test_support.h"

bool test_display(void)
{
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

    fake_reset();
    minishell_services_port_t no_display = fake_full_port();
    no_display.display_present = NULL;
    minishell_services_configure(&no_display);
    TEST_CHECK(mini_api_get()->display == NULL);
    return true;
}

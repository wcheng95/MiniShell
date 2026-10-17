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
static uint32_t raster_width = 240, raster_height = 135, blits;
static const uint16_t *captured_pixels;
static uint32_t captured_x, captured_y, captured_width, captured_height, captured_stride;
static mini_result_t raster_info(void *ctx, uint32_t *w, uint32_t *h)
{ (void)ctx; *w = raster_width; *h = raster_height; return MINI_OK; }
static mini_result_t blit(void *ctx, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                          const uint16_t *pixels, uint32_t stride)
{
    (void)ctx; ++blits; captured_x=x; captured_y=y; captured_width=w;
    captured_height=h; captured_pixels=pixels; captured_stride=stride; return MINI_OK;
}
static bool test_raster(void)
{
    minishell_services_port_t p = fake_full_port();
    minishell_services_configure(&p);
    const mini_display_api_t *d = mini_api_get()->display;
    TEST_CHECK(!(d->capabilities & MINI_DISPLAY_CAP_RASTER_RGB565) && !d->raster);
    p.display_capabilities |= MINI_DISPLAY_CAP_RASTER_RGB565;
    p.display_raster_get_info = raster_info;
    minishell_services_configure(&p);
    TEST_CHECK(!(d->capabilities & MINI_DISPLAY_CAP_RASTER_RGB565) && !d->raster);
    p.display_blit_rgb565 = blit;
    p.display_raster_get_info = NULL;
    minishell_services_configure(&p);
    TEST_CHECK(!d->raster);
    p.display_raster_get_info = raster_info;
    p.display_capabilities &= ~MINI_DISPLAY_CAP_RASTER_RGB565;
    minishell_services_configure(&p);
    TEST_CHECK(!d->raster);
    p.display_capabilities |= MINI_DISPLAY_CAP_RASTER_RGB565;
    minishell_services_configure(&p);
    TEST_CHECK(d->raster && (d->capabilities & MINI_DISPLAY_CAP_RASTER_RGB565));
    const mini_raster_display_api_t *r = d->raster;
    mini_raster_display_info_t info = {.struct_size = sizeof(info)};
    TEST_EQ(r->get_info(NULL), MINI_ERR_INVALID);
    info.struct_size = sizeof(uint32_t);
    TEST_EQ(r->get_info(&info), MINI_ERR_INVALID);
    info.struct_size = sizeof(info);
    TEST_EQ(r->get_info(&info), MINI_OK);
    TEST_EQ(info.width, 240u); TEST_EQ(info.height, 135u);
    raster_width = 0;
    TEST_EQ(r->get_info(&info), MINI_ERR_NOT_READY);
    raster_width = 240; raster_height = 0;
    TEST_EQ(r->get_info(&info), MINI_ERR_NOT_READY);
    raster_height = 135;
    uint16_t pixels[20] = {0};
    blits = 0;
    TEST_EQ(r->blit_rgb565(UINT32_MAX, UINT32_MAX, 0, 1, NULL, 0), MINI_OK);
    TEST_EQ(r->blit_rgb565(0, 0, 1, 0, NULL, 0), MINI_OK);
    TEST_EQ(blits, 0u);
    TEST_EQ(r->blit_rgb565(0, 0, 1, 1, NULL, 1), MINI_ERR_INVALID);
    TEST_EQ(r->blit_rgb565(0, 0, 10, 1, pixels, 9), MINI_ERR_INVALID);
    TEST_EQ(r->blit_rgb565(239, 0, 2, 1, pixels, 2), MINI_ERR_INVALID);
    TEST_EQ(r->blit_rgb565(0, 134, 1, 2, pixels, 1), MINI_ERR_INVALID);
    TEST_EQ(r->blit_rgb565(UINT32_MAX, 0, 1, 1, pixels, 1), MINI_ERR_INVALID);
    TEST_EQ(r->blit_rgb565(0, 0, UINT32_MAX, 1, pixels, UINT32_MAX), MINI_ERR_INVALID);
    TEST_EQ(blits, 0u);
    TEST_EQ(r->blit_rgb565(2, 18, 3, 2, pixels, 10), MINI_OK);
    TEST_EQ(blits, 1u); TEST_CHECK(captured_pixels == pixels);
    TEST_EQ(captured_x, 2u); TEST_EQ(captured_y, 18u); TEST_EQ(captured_width, 3u);
    TEST_EQ(captured_height, 2u); TEST_EQ(captured_stride, 10u);
    TEST_EQ(MINISHELL_API_VERSION, 3u);
    return true;
}
bool test_display(void)
{
    TEST_CHECK(test_raster());
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

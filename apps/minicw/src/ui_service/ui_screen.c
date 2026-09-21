/* Preserve semantic colors; MiniShell owns rendering geometry. */
#include "ui_screen.h"
#include "minicw_port.h"
#include <string.h>
void ui_screen_init(void) { }
void ui_screen_render(const mini_cw_screen_t *screen)
{
    char rows[7][21];
    uint8_t colors[7][20];
    if (!screen) return;
    for (unsigned r = 0; r < 7; ++r) {
        const char *text = r ? screen->line[r - 1] : screen->top;
        memset(rows[r], ' ', 20);
        rows[r][20] = '\0';
        for (unsigned c = 0; c < 20 && text[c]; ++c) rows[r][c] = text[c];
        for (unsigned c = 0; c < 20; ++c) {
            mini_cw_screen_color_t color = r ? screen->line_color[r - 1] : screen->top_color[c];
            if (color == MINI_CW_SCREEN_COLOR_DEFAULT)
                color = r == 0 ? MINI_CW_SCREEN_COLOR_WHITE : r == 6 ? MINI_CW_SCREEN_COLOR_CYAN : MINI_CW_SCREEN_COLOR_GREEN;
            colors[r][c] = (uint8_t)color;
        }
    }
    minicw_port_present((const char (*)[21])rows, (const uint8_t (*)[20])colors);
}

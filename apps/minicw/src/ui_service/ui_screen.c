/* MiniShell's 20x7 text display has no color or pixel separator. */
#include "ui_screen.h"
#include "minicw_port.h"
#include <string.h>
void ui_screen_init(void) { }
void ui_screen_render(const mini_cw_screen_t *screen)
{
    char rows[7][21];
    if (!screen) return;
    for (unsigned r = 0; r < 7; ++r) {
        const char *text = r ? screen->line[r - 1] : screen->top;
        memset(rows[r], ' ', 20);
        rows[r][20] = '\0';
        for (unsigned c = 0; c < 20 && text[c]; ++c) rows[r][c] = text[c];
    }
    minicw_port_present((const char (*)[21])rows);
}

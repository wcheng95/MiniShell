#include "adv_ft8_web_io.h"
#include <string.h>

bool adv_mirror_parse_key(const char *query, mini_key_event_t *event)
{
    memset(event, 0, sizeof(*event));
    if (!query) return false;
    size_t length = 0;
    while (length < ADV_MIRROR_QUERY_CAP && query[length]) ++length;
    if (!length || length == ADV_MIRROR_QUERY_CAP) return false;
    bool character = false, special = false, modifiers = false;
    uint32_t value = 0, mods = 0;
    size_t i = 0;
    while (i < length) {
        char field = query[i++];
        if (i >= length || query[i++] != '=') return false;
        size_t start = i;
        uint32_t number = 0;
        while (i < length && query[i] >= '0' && query[i] <= '9') {
            number = number * 10u + (uint32_t)(query[i++] - '0');
            if (number > 127u) return false;
        }
        if (i == start) return false;
        if (field == 'm' && !modifiers) { modifiers = true; mods = number; }
        else if (field == 'c' && !character && !special) { character = true; value = number; }
        else if (field == 'k' && !character && !special) { special = true; value = number; }
        else return false;
        if (i < length && (query[i++] != '&' || i == length)) return false;
    }
    if (!modifiers || (!character && !special) ||
        (mods & ~(MINI_MOD_SHIFT | MINI_MOD_CTRL | MINI_MOD_ALT | MINI_MOD_FN | MINI_MOD_OPT))) return false;
    if (character) {
        if (value < 32 || value > 126) return false;
    } else {
        switch (value) {
        case MINI_KEY_ESCAPE: case MINI_KEY_ENTER: case MINI_KEY_BACKSPACE:
        case MINI_KEY_UP: case MINI_KEY_DOWN: case MINI_KEY_LEFT: case MINI_KEY_RIGHT:
        case MINI_KEY_PAGE_UP: case MINI_KEY_PAGE_DOWN: case MINI_KEY_TAB: break;
        default: return false;
        }
    }
    event->struct_size = sizeof(*event);
    event->type = character ? MINI_KEY_EVENT_CHAR : MINI_KEY_EVENT_SPECIAL;
    event->codepoint = character ? value : 0;
    event->key = special ? value : 0;
    event->modifiers = mods;
    return true;
}

void adv_mirror_encode_screen(const adv_display_snapshot_t *screen, uint8_t *out)
{
    for (unsigned i = 0; i < 4; ++i) out[i] = (uint8_t)(screen->generation >> (8u * i));
    memcpy(out + 4, screen->cells, ADV_MIRROR_CELLS);
    memcpy(out + 4 + ADV_MIRROR_CELLS, screen->attrs, ADV_MIRROR_CELLS);
}

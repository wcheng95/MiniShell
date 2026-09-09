#include "presentation_profile.h"

#include <stddef.h>

static int ascii_tolower(int ch)
{
    return (ch >= 'A' && ch <= 'Z') ? ch + ('a' - 'A') : ch;
}

static bool name_equals(const char *a, const char *b)
{
    if (a == NULL || b == NULL) return false;
    while (*a != '\0' && *b != '\0') {
        if (ascii_tolower((unsigned char)*a) != ascii_tolower((unsigned char)*b)) return false;
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

bool ft8_presentation_parse(const char *name, ft8_presentation_profile_t *out_profile)
{
    if (out_profile == NULL) return false;
    if (name_equals(name, "desktop")) {
        *out_profile = FT8_PRESENTATION_DESKTOP;
        return true;
    }
    if (name_equals(name, "adv")) {
        *out_profile = FT8_PRESENTATION_ADV;
        return true;
    }
    return false;
}

const char *ft8_presentation_name(ft8_presentation_profile_t profile)
{
    switch (profile) {
        case FT8_PRESENTATION_DESKTOP: return "DESKTOP";
        case FT8_PRESENTATION_ADV: return "ADV";
        default: return "?";
    }
}

bool ft8_presentation_get_spec(ft8_presentation_profile_t profile,
                               ft8_presentation_spec_t *out_spec)
{
    if (out_spec == NULL) return false;

    switch (profile) {
        case FT8_PRESENTATION_DESKTOP:
            out_spec->columns = 30u;
            out_spec->rows = 8u;
            out_spec->has_footer = true;
            return true;
        case FT8_PRESENTATION_ADV:
            out_spec->columns = 20u;
            out_spec->rows = 7u;
            out_spec->has_footer = false;
            return true;
        default:
            return false;
    }
}

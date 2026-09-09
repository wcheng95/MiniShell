#ifndef FT8_PRESENTATION_PROFILE_H
#define FT8_PRESENTATION_PROFILE_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    FT8_PRESENTATION_DESKTOP = 0,
    FT8_PRESENTATION_ADV
} ft8_presentation_profile_t;

typedef struct {
    uint32_t columns;
    uint32_t rows;
    bool has_footer;
} ft8_presentation_spec_t;

bool ft8_presentation_parse(const char *name, ft8_presentation_profile_t *out_profile);
const char *ft8_presentation_name(ft8_presentation_profile_t profile);
bool ft8_presentation_get_spec(ft8_presentation_profile_t profile,
                               ft8_presentation_spec_t *out_spec);

#endif

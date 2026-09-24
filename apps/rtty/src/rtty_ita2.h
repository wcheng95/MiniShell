#ifndef RTTY_ITA2_H
#define RTTY_ITA2_H
#include <stdint.h>
/* Zero for shifts, NUL and invalid symbols. No USOS. */
char rtty_ita2_decode(uint8_t *figures, unsigned code);
#endif

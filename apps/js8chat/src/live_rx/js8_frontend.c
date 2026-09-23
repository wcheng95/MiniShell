#include "js8_frontend.h"
#include <limits.h>
int js8_frontend_process(Js8Frontend *s, const int16_t *stereo, size_t frames,
                         float *out, size_t capacity, size_t *count)
{
    if (!s || !stereo || !out || !count || s->phase > 1 || frames > SIZE_MAX/2) return -1;
    size_t need = frames/2 + ((frames & 1) && !s->phase);
    if (need > capacity) return -1;
    size_t n = 0;
    for (size_t i = 0; i < frames; ++i) {
        if (!s->phase) out[n++] = ((int32_t)stereo[2*i] + stereo[2*i+1]) / 65536.0f;
        s->phase ^= 1;
    }
    *count = n;
    return 0;
}
int js8_live_anchor(int64_t seconds, uint32_t ns, size_t produced,
                    int64_t *slot, uint32_t *offset)
{
    if (!slot || !offset || ns >= 1000000000u || produced > 90000) return -1;
    int64_t id = seconds/15, rem = seconds%15;
    if (rem < 0) { --id; rem += 15; }
    int64_t sample = rem*6000 + (int64_t)((uint64_t)ns*6000/1000000000u) - (int64_t)produced;
    if (sample < 0) { --id; sample += 90000; }
    *slot = id; *offset = (uint32_t)sample;
    return 0;
}

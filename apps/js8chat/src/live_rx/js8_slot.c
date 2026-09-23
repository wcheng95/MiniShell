#include "js8_slot.h"

/* MiniFT8 live scheduling: UTC locates pre-roll; counting fills one capture. */
int js8_slot_feed(Js8SlotScheduler *s, uint32_t slot, uint32_t offset,
                   const float *samples, size_t count, Js8SlotSink sink, void *ctx)
{
    if (!s || !samples || !sink || offset >= 90000 || count > 90000) return -1;
    if (!count) return 0;
    int64_t pos = (int64_t)slot*90000 + offset, end = pos + (int64_t)count;
    if (!s->scheduled) {
        s->next_slot = (uint64_t)(pos+9600)/90000;
        int64_t pre = (int64_t)s->next_slot*90000-9600;
        if (pos > pre+240) ++s->next_slot;
        s->scheduled = 1;
    }
    size_t index = 0;
    while (pos < end) {
        int64_t pre = (int64_t)s->next_slot*90000-9600;
        if (pre <= pos) {
            if (s->active && sink(ctx, JS8_SLOT_DROP, s->capture_slot, NULL, 0)) return -1;
            if (s->next_slot > UINT32_MAX) return -1;
            s->capture_slot = (uint32_t)s->next_slot++;
            s->captured = 0; s->active = 1;
            if (sink(ctx, JS8_SLOT_BEGIN, s->capture_slot, NULL, 0)) return -1;
            continue;
        }
        size_t n = (size_t)((end < pre ? end : pre) - pos);
        if (s->active) {
            size_t take = n;
            if (take > 89280 - s->captured) take = 89280 - s->captured;
            if (take && sink(ctx, JS8_SLOT_SAMPLES, s->capture_slot, samples+index, take)) return -1;
            s->captured += (uint32_t)take;
            if (s->captured == 89280) {
                s->active = 0;
                if (sink(ctx, JS8_SLOT_READY, s->capture_slot, NULL, 0)) return -1;
            }
        }
        pos += (int64_t)n; index += n;
    }
    return 0;
}

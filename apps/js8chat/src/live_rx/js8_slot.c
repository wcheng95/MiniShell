#include "js8_slot.h"
int js8_slot_feed(Js8SlotScheduler *s, uint32_t slot, uint32_t offset,
                   const float *samples, size_t count, Js8SlotSink sink, void *ctx)
{
    if (!s || !samples || !sink || offset >= 90000 || count > 90000) return -1;
    uint64_t pos = (uint64_t)slot*90000 + offset, end = pos + count;
    if (!s->scheduled) {
        s->next_slot = slot + (uint64_t)(offset != 0); s->scheduled = 1;
    }
    if (s->next_slot*90000 < pos) {
        uint32_t missed = s->active ? s->capture_slot : (uint32_t)s->next_slot;
        if (sink(ctx, JS8_SLOT_DROP, missed, NULL, 0)) return -1;
        s->active = 0;
        s->next_slot = (pos+89999)/90000;
    }
    size_t index = 0;
    while (pos < end) {
        uint64_t boundary = s->next_slot*90000;
        if (pos == boundary) {
            if (s->active && sink(ctx, JS8_SLOT_DROP, s->capture_slot, NULL, 0)) return -1;
            if (s->next_slot > UINT32_MAX) return -1;
            s->capture_slot = (uint32_t)s->next_slot++;
            s->captured = 0; s->active = 1;
            if (sink(ctx, JS8_SLOT_BEGIN, s->capture_slot, NULL, 0)) return -1;
            boundary = s->next_slot*90000;
        }
        size_t n = (size_t)((end < boundary ? end : boundary) - pos);
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
        pos += n; index += n;
    }
    return 0;
}

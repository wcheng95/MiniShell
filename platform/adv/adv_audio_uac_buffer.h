#ifndef ADV_AUDIO_UAC_BUFFER_H
#define ADV_AUDIO_UAC_BUFFER_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define ADV_UAC_RING_FRAMES 2048u
/* All operations are serialized by the provider's short critical section. */
typedef struct {
    int16_t frames[ADV_UAC_RING_FRAMES][2];
    uint32_t head, tail, epoch, high_water, losses, overflows;
    uint8_t partial[6], used, phase;
    bool pending, reset_required;
} adv_uac_buffer_t;

typedef struct { uint32_t epoch; bool discard; } adv_uac_ticket_t;

static inline void adv_uac_loss(adv_uac_buffer_t *b)
{
    ++b->epoch;
    ++b->losses;
    b->head = b->tail = 0;
    b->used = b->phase = 0;
    b->pending = true;
}

static inline bool adv_uac_ack(adv_uac_buffer_t *b)
{
    if (!b->pending) return false;
    b->pending = false;
    b->reset_required = true;
    return true;
}

static inline adv_uac_ticket_t adv_uac_begin(const adv_uac_buffer_t *b)
{
    adv_uac_ticket_t ticket = {b->epoch, b->pending || b->reset_required};
    return ticket;
}

static inline int16_t adv_uac_s24(const uint8_t *p)
{
    /* Dropping the low byte is exactly arithmetic S24 >> 8, without relying
     * on implementation-defined signed shifts or sign-extension overflow. */
    int32_t high = (int32_t)p[1] | ((int32_t)p[2] << 8);
    return (int16_t)(high >= 32768 ? high - 65536 : high);
}

static inline bool adv_uac_feed(adv_uac_buffer_t *b, adv_uac_ticket_t ticket,
                                const uint8_t *data, uint32_t bytes)
{
    if (ticket.discard || ticket.epoch != b->epoch || b->pending || b->reset_required)
        return true;
    for (uint32_t i = 0; i < bytes; ++i) {
        b->partial[b->used++] = data[i];
        if (b->used != 6) continue;
        b->used = 0;
        if (b->phase == 0) {
            if (b->head - b->tail == ADV_UAC_RING_FRAMES) {
                /*
                 * Preserve the newest UTC-aligned timeline under temporary
                 * consumer backlog.  Dropping one stale 12 kHz frame costs
                 * 83.3 us; escalating this local FIFO condition into a stream
                 * discontinuity would discard the active FT8 slot.
                 */
                ++b->overflows;
                ++b->tail;
            }
            uint32_t slot = b->head++ & (ADV_UAC_RING_FRAMES - 1u);
            b->frames[slot][0] = adv_uac_s24(b->partial);
            b->frames[slot][1] = adv_uac_s24(b->partial + 3);
            uint32_t count = b->head - b->tail;
            if (count > b->high_water) b->high_water = count;
        }
        b->phase = (b->phase + 1u) & 3u;
    }
    return true;
}

static inline uint32_t adv_uac_take(adv_uac_buffer_t *b, int16_t *out, uint32_t capacity)
{
    if (b->pending || b->reset_required) return 0;
    uint32_t count = b->head - b->tail;
    if (count > capacity) count = capacity;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t slot = b->tail++ & (ADV_UAC_RING_FRAMES - 1u);
        *out++ = b->frames[slot][0];
        *out++ = b->frames[slot][1];
    }
    return count;
}
#endif

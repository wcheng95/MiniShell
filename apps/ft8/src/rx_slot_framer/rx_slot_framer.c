#include "rx_slot_framer.h"

#include <limits.h>
#include <string.h>

static RxSlotFramerStatus emit_event(RxSlotFramer *framer,
                                     RxSlotFramerEmitFn emit,
                                     void *emit_ctx,
                                     RxSlotFramerEventType type,
                                     int64_t slot_id,
                                     const float *samples,
                                     size_t sample_count)
{
    RxSlotFramerEvent event;

    if (!framer || !emit)
        return RX_SLOT_FRAMER_ERR_INVALID;

    event.type = type;
    event.slot_id = slot_id;
    event.samples = samples;
    event.sample_count = sample_count;

    if (emit(emit_ctx, &event) != 0) {
        framer->faulted = 1;
        return RX_SLOT_FRAMER_ERR_SINK;
    }

    return RX_SLOT_FRAMER_OK;
}

static RxSlotFramerStatus advance_slot(RxSlotFramer *framer)
{
    if (!framer)
        return RX_SLOT_FRAMER_ERR_INVALID;
    if (framer->slot_id == INT64_MAX) {
        framer->faulted = 1;
        return RX_SLOT_FRAMER_ERR_RANGE;
    }

    framer->slot_id++;
    framer->sample_offset = 0u;
    return RX_SLOT_FRAMER_OK;
}

RxSlotFramerStatus rx_slot_framer_init(RxSlotFramer *framer,
                                       int64_t slot_id,
                                       uint32_t sample_offset)
{
    if (!framer || sample_offset >= RX_SLOT_FRAMER_SLOT_SAMPLES)
        return RX_SLOT_FRAMER_ERR_INVALID;

    memset(framer, 0, sizeof(*framer));
    framer->slot_id = slot_id;
    framer->sample_offset = sample_offset;
    framer->waiting_for_full_boundary = (sample_offset != 0u);
    framer->initialized = 1;
    return RX_SLOT_FRAMER_OK;
}

void rx_slot_framer_destroy(RxSlotFramer *framer)
{
    if (!framer)
        return;
    memset(framer, 0, sizeof(*framer));
}

RxSlotFramerStatus rx_slot_framer_process(RxSlotFramer *framer,
                                          const float *samples,
                                          size_t sample_count,
                                          RxSlotFramerEmitFn emit,
                                          void *emit_ctx)
{
    size_t input_index = 0u;

    if (!framer)
        return RX_SLOT_FRAMER_ERR_INVALID;
    if (!framer->initialized)
        return RX_SLOT_FRAMER_ERR_NOT_INITIALIZED;
    if (framer->faulted)
        return RX_SLOT_FRAMER_ERR_STATE;
    if (sample_count == 0u)
        return RX_SLOT_FRAMER_OK;
    if (!samples || !emit)
        return RX_SLOT_FRAMER_ERR_INVALID;

    while (input_index < sample_count) {
        if (framer->waiting_for_full_boundary) {
            uint32_t to_boundary = RX_SLOT_FRAMER_SLOT_SAMPLES - framer->sample_offset;
            size_t available = sample_count - input_index;
            size_t skip = available < (size_t)to_boundary ? available : (size_t)to_boundary;

            input_index += skip;
            framer->sample_offset += (uint32_t)skip;

            if (framer->sample_offset == RX_SLOT_FRAMER_SLOT_SAMPLES) {
                RxSlotFramerStatus status = advance_slot(framer);
                if (status != RX_SLOT_FRAMER_OK)
                    return status;
                framer->waiting_for_full_boundary = 0;
            }
            continue;
        }

        if (!framer->window_active) {
            RxSlotFramerStatus status;

            if (framer->sample_offset != 0u) {
                framer->faulted = 1;
                return RX_SLOT_FRAMER_ERR_STATE;
            }

            status = emit_event(framer, emit, emit_ctx,
                                RX_SLOT_FRAMER_EVENT_BEGIN_WINDOW,
                                framer->slot_id, NULL, 0u);
            if (status != RX_SLOT_FRAMER_OK)
                return status;
            framer->window_active = 1;
        }

        while (input_index < sample_count &&
               framer->sample_offset < RX_SLOT_FRAMER_SLOT_SAMPLES) {
            uint32_t slot_remaining = RX_SLOT_FRAMER_SLOT_SAMPLES - framer->sample_offset;
            size_t input_remaining = sample_count - input_index;
            size_t block_remaining = RX_SLOT_FRAMER_BLOCK_SAMPLES - framer->block_fill;
            size_t take = input_remaining;

            if (take > (size_t)slot_remaining)
                take = (size_t)slot_remaining;
            if (take > block_remaining)
                take = block_remaining;

            memcpy(&framer->block[framer->block_fill],
                   &samples[input_index],
                   take * sizeof(float));
            framer->block_fill += take;
            framer->sample_offset += (uint32_t)take;
            input_index += take;

            if (framer->block_fill == RX_SLOT_FRAMER_BLOCK_SAMPLES) {
                RxSlotFramerStatus status = emit_event(
                    framer, emit, emit_ctx,
                    RX_SLOT_FRAMER_EVENT_ENGINE_BLOCK,
                    framer->slot_id,
                    framer->block,
                    RX_SLOT_FRAMER_BLOCK_SAMPLES);
                if (status != RX_SLOT_FRAMER_OK)
                    return status;
                framer->block_fill = 0u;
            }

            if (framer->sample_offset == RX_SLOT_FRAMER_SLOT_SAMPLES) {
                RxSlotFramerStatus status;

                /* Never mix a partial engine block across FT8 slot boundaries. */
                framer->block_fill = 0u;

                status = emit_event(framer, emit, emit_ctx,
                                    RX_SLOT_FRAMER_EVENT_FINALIZE_WINDOW,
                                    framer->slot_id, NULL, 0u);
                if (status != RX_SLOT_FRAMER_OK)
                    return status;
                framer->window_active = 0;

                status = advance_slot(framer);
                if (status != RX_SLOT_FRAMER_OK)
                    return status;
                break;
            }
        }
    }

    return RX_SLOT_FRAMER_OK;
}

RxSlotFramerStatus rx_slot_framer_reset_stream(RxSlotFramer *framer,
                                               int64_t slot_id,
                                               uint32_t sample_offset,
                                               RxSlotFramerEmitFn emit,
                                               void *emit_ctx)
{
    RxSlotFramerStatus status;

    if (!framer || !emit || sample_offset >= RX_SLOT_FRAMER_SLOT_SAMPLES)
        return RX_SLOT_FRAMER_ERR_INVALID;
    if (!framer->initialized)
        return RX_SLOT_FRAMER_ERR_NOT_INITIALIZED;

    framer->faulted = 0;
    framer->slot_id = slot_id;
    framer->sample_offset = sample_offset;
    framer->waiting_for_full_boundary = (sample_offset != 0u);
    framer->window_active = 0;
    framer->block_fill = 0u;

    status = emit_event(framer, emit, emit_ctx,
                        RX_SLOT_FRAMER_EVENT_STREAM_RESET,
                        slot_id, NULL, 0u);
    return status;
}

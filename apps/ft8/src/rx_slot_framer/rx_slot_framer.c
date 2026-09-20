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

static void advance_compat_time(RxSlotFramer *framer, size_t samples)
{
    uint64_t total;

    total = (uint64_t)framer->sample_offset + samples;
    while (total >= RX_SLOT_FRAMER_SLOT_SAMPLES) {
        total -= RX_SLOT_FRAMER_SLOT_SAMPLES;
        if (framer->slot_id != INT64_MAX)
            ++framer->slot_id;
    }
    framer->sample_offset = (uint32_t)total;
}

static RxSlotFramerStatus on_scheduled_block(RxSlotFramer *framer,
                                             RxSlotFramerEmitFn emit,
                                             void *emit_ctx)
{
    RxSlotFramerStatus status;

    if (!framer->capture_active)
        return RX_SLOT_FRAMER_OK;

    if (framer->capture_block_count >= RX_SLOT_FRAMER_WINDOW_BLOCKS)
        return RX_SLOT_FRAMER_OK;

    status = emit_event(framer, emit, emit_ctx,
                        RX_SLOT_FRAMER_EVENT_ENGINE_BLOCK,
                        framer->capture_slot_id,
                        framer->block,
                        RX_SLOT_FRAMER_BLOCK_SAMPLES);
    if (status != RX_SLOT_FRAMER_OK)
        return status;

    ++framer->capture_block_count;

    if (!framer->capture_begin_emitted &&
        framer->capture_block_count == RX_SLOT_FRAMER_PREROLL_BLOCKS) {
        status = emit_event(framer, emit, emit_ctx,
                            RX_SLOT_FRAMER_EVENT_BEGIN_WINDOW,
                            framer->capture_slot_id, NULL, 0u);
        if (status != RX_SLOT_FRAMER_OK)
            return status;
        framer->capture_begin_emitted = 1;
    }

    if (!framer->capture_primary_emitted &&
        framer->capture_block_count == RX_SLOT_FRAMER_DECODE_WINDOW_BLOCK) {
        status = emit_event(framer, emit, emit_ctx,
                            RX_SLOT_FRAMER_EVENT_FINALIZE_WINDOW,
                            framer->capture_slot_id, NULL, 0u);
        if (status != RX_SLOT_FRAMER_OK)
            return status;
        framer->capture_primary_emitted = 1;
    }

    return RX_SLOT_FRAMER_OK;
}

static RxSlotFramerStatus on_compat_block(RxSlotFramer *framer,
                                          RxSlotFramerEmitFn emit,
                                          void *emit_ctx)
{
    RxSlotFramerStatus status;

    status = emit_event(framer, emit, emit_ctx,
                        RX_SLOT_FRAMER_EVENT_ENGINE_BLOCK,
                        framer->slot_id,
                        framer->block,
                        RX_SLOT_FRAMER_BLOCK_SAMPLES);
    if (status != RX_SLOT_FRAMER_OK)
        return status;

    if (!framer->slot_anchor_valid)
        return RX_SLOT_FRAMER_OK;

    ++framer->slot_block_count;
    if (!framer->primary_emitted &&
        framer->slot_block_count >= RX_SLOT_FRAMER_DECODE_BLOCKS) {
        status = emit_event(framer, emit, emit_ctx,
                            RX_SLOT_FRAMER_EVENT_FINALIZE_WINDOW,
                            framer->slot_id, NULL, 0u);
        if (status != RX_SLOT_FRAMER_OK)
            return status;
        framer->primary_emitted = 1;
    }
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
    framer->begin_pending = (sample_offset == 0u);
    framer->initialized = 1;
    return RX_SLOT_FRAMER_OK;
}

void rx_slot_framer_destroy(RxSlotFramer *framer)
{
    if (framer)
        memset(framer, 0, sizeof(*framer));
}

RxSlotFramerStatus rx_slot_framer_start_capture(RxSlotFramer *framer,
                                                int64_t slot_id,
                                                RxSlotFramerEmitFn emit,
                                                void *emit_ctx)
{
    RxSlotFramerStatus status;

    if (!framer || !emit)
        return RX_SLOT_FRAMER_ERR_INVALID;
    if (!framer->initialized)
        return RX_SLOT_FRAMER_ERR_NOT_INITIALIZED;
    if (framer->faulted)
        return RX_SLOT_FRAMER_ERR_STATE;

    framer->scheduled_mode = 1;
    framer->capture_active = 1;
    framer->capture_slot_id = slot_id;
    framer->capture_block_count = 0u;
    framer->capture_begin_emitted = 0;
    framer->capture_primary_emitted = 0;
    framer->block_fill = 0u;

    status = emit_event(framer, emit, emit_ctx,
                        RX_SLOT_FRAMER_EVENT_CAPTURE_RESET,
                        slot_id, NULL, 0u);
    return status;
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

    if (framer->scheduled_mode) {
        if (!framer->capture_active) {
            advance_compat_time(framer, sample_count);
            return RX_SLOT_FRAMER_OK;
        }

        while (input_index < sample_count) {
            size_t input_remaining = sample_count - input_index;
            size_t block_remaining =
                RX_SLOT_FRAMER_BLOCK_SAMPLES - framer->block_fill;
            size_t take =
                input_remaining < block_remaining ? input_remaining : block_remaining;
            RxSlotFramerStatus status;

            memcpy(&framer->block[framer->block_fill],
                   &samples[input_index],
                   take * sizeof(float));
            framer->block_fill += take;
            input_index += take;
            advance_compat_time(framer, take);

            if (framer->block_fill == RX_SLOT_FRAMER_BLOCK_SAMPLES) {
                status = on_scheduled_block(framer, emit, emit_ctx);
                if (status != RX_SLOT_FRAMER_OK)
                    return status;
                framer->block_fill = 0u;
            }
        }
        return RX_SLOT_FRAMER_OK;
    }

    /* Compatibility path retained for explicit-timing/offline RX. */
    while (input_index < sample_count) {
        uint32_t slot_remaining;
        size_t input_remaining;
        size_t block_remaining;
        size_t take;
        RxSlotFramerStatus status;

        if (framer->begin_pending) {
            status = emit_event(framer, emit, emit_ctx,
                                RX_SLOT_FRAMER_EVENT_BEGIN_WINDOW,
                                framer->slot_id, NULL, 0u);
            if (status != RX_SLOT_FRAMER_OK)
                return status;
            framer->begin_pending = 0;
            framer->slot_anchor_valid = 1;
            framer->slot_block_count = 0u;
            framer->primary_emitted = 0;
        }

        slot_remaining = RX_SLOT_FRAMER_SLOT_SAMPLES - framer->sample_offset;
        input_remaining = sample_count - input_index;
        block_remaining = RX_SLOT_FRAMER_BLOCK_SAMPLES - framer->block_fill;
        take = input_remaining;
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
            status = on_compat_block(framer, emit, emit_ctx);
            if (status != RX_SLOT_FRAMER_OK)
                return status;
            framer->block_fill = 0u;
        }

        if (framer->sample_offset == RX_SLOT_FRAMER_SLOT_SAMPLES) {
            if (framer->slot_id == INT64_MAX) {
                framer->faulted = 1;
                return RX_SLOT_FRAMER_ERR_RANGE;
            }
            ++framer->slot_id;
            framer->sample_offset = 0u;
            framer->slot_anchor_valid = 0;
            framer->begin_pending = 1;
            framer->slot_block_count = 0u;
            framer->primary_emitted = 0;
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
    if (!framer || !emit || sample_offset >= RX_SLOT_FRAMER_SLOT_SAMPLES)
        return RX_SLOT_FRAMER_ERR_INVALID;
    if (!framer->initialized)
        return RX_SLOT_FRAMER_ERR_NOT_INITIALIZED;

    framer->faulted = 0;
    framer->slot_id = slot_id;
    framer->sample_offset = sample_offset;
    framer->slot_anchor_valid = 0;
    framer->begin_pending = (sample_offset == 0u);
    framer->slot_block_count = 0u;
    framer->primary_emitted = 0;
    framer->scheduled_mode = 0;
    framer->capture_active = 0;
    framer->capture_block_count = 0u;
    framer->capture_begin_emitted = 0;
    framer->capture_primary_emitted = 0;
    framer->block_fill = 0u;

    return emit_event(framer, emit, emit_ctx,
                      RX_SLOT_FRAMER_EVENT_STREAM_RESET,
                      slot_id, NULL, 0u);
}

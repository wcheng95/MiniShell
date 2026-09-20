#ifndef RX_SLOT_FRAMER_H
#define RX_SLOT_FRAMER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RX_SLOT_FRAMER_SAMPLE_RATE_HZ 6000u
#define RX_SLOT_FRAMER_SLOT_SECONDS 15u
#define RX_SLOT_FRAMER_SLOT_SAMPLES \
    (RX_SLOT_FRAMER_SAMPLE_RATE_HZ * RX_SLOT_FRAMER_SLOT_SECONDS)
#define RX_SLOT_FRAMER_BLOCK_SAMPLES 960u

#define RX_SLOT_FRAMER_PREROLL_BLOCKS 10u
#define RX_SLOT_FRAMER_PREROLL_SAMPLES \
    (RX_SLOT_FRAMER_PREROLL_BLOCKS * RX_SLOT_FRAMER_BLOCK_SAMPLES)
#define RX_SLOT_FRAMER_DECODE_BLOCKS 79u
#define RX_SLOT_FRAMER_DECODE_WINDOW_BLOCK \
    (RX_SLOT_FRAMER_PREROLL_BLOCKS + RX_SLOT_FRAMER_DECODE_BLOCKS)
#define RX_SLOT_FRAMER_WINDOW_BLOCKS 93u
#define RX_SLOT_FRAMER_REANCHOR_OFFSET \
    (RX_SLOT_FRAMER_SLOT_SAMPLES - RX_SLOT_FRAMER_PREROLL_SAMPLES)

typedef enum {
    RX_SLOT_FRAMER_OK = 0,
    RX_SLOT_FRAMER_ERR_INVALID = -1,
    RX_SLOT_FRAMER_ERR_NOT_INITIALIZED = -2,
    RX_SLOT_FRAMER_ERR_STATE = -3,
    RX_SLOT_FRAMER_ERR_SINK = -4,
    RX_SLOT_FRAMER_ERR_RANGE = -5
} RxSlotFramerStatus;

typedef enum {
    RX_SLOT_FRAMER_EVENT_BEGIN_WINDOW = 0,
    RX_SLOT_FRAMER_EVENT_ENGINE_BLOCK = 1,
    RX_SLOT_FRAMER_EVENT_FINALIZE_WINDOW = 2,
    RX_SLOT_FRAMER_EVENT_STREAM_RESET = 3,
    RX_SLOT_FRAMER_EVENT_CAPTURE_RESET = 4
} RxSlotFramerEventType;

typedef struct {
    RxSlotFramerEventType type;
    int64_t slot_id;
    const float *samples;
    size_t sample_count;
} RxSlotFramerEvent;

typedef int (*RxSlotFramerEmitFn)(void *ctx,
                                  const RxSlotFramerEvent *event);

typedef struct {
    int initialized;
    int faulted;

    /* Compatibility UTC/sample timeline used by explicit-timing RX. */
    int64_t slot_id;
    uint32_t sample_offset;
    int slot_anchor_valid;
    int begin_pending;
    uint32_t slot_block_count;
    int primary_emitted;

    /* Live V3 slot-local capture mode. */
    int scheduled_mode;
    int capture_active;
    int64_t capture_slot_id;
    uint32_t capture_block_count;
    int capture_begin_emitted;
    int capture_primary_emitted;

    size_t block_fill;
    float block[RX_SLOT_FRAMER_BLOCK_SAMPLES];
} RxSlotFramer;

RxSlotFramerStatus rx_slot_framer_init(RxSlotFramer *framer,
                                       int64_t slot_id,
                                       uint32_t sample_offset);

void rx_slot_framer_destroy(RxSlotFramer *framer);

/* Start a fresh live capture exactly at UTC-1.6 s for slot_id.  The partial
 * 160-ms block is discarded and CAPTURE_RESET is emitted before new samples. */
RxSlotFramerStatus rx_slot_framer_start_capture(RxSlotFramer *framer,
                                                int64_t slot_id,
                                                RxSlotFramerEmitFn emit,
                                                void *emit_ctx);

RxSlotFramerStatus rx_slot_framer_process(RxSlotFramer *framer,
                                          const float *samples,
                                          size_t sample_count,
                                          RxSlotFramerEmitFn emit,
                                          void *emit_ctx);

RxSlotFramerStatus rx_slot_framer_reset_stream(RxSlotFramer *framer,
                                               int64_t slot_id,
                                               uint32_t sample_offset,
                                               RxSlotFramerEmitFn emit,
                                               void *emit_ctx);

#ifdef __cplusplus
}
#endif

#endif

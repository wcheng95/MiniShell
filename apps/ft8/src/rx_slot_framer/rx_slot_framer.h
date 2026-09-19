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

/* Primary candidate search begins as soon as 79 logical FT8 blocks have
 * completed relative to the latched UTC slot anchor. */
#define RX_SLOT_FRAMER_DECODE_BLOCKS 79u

/* A cheap second candidate search is always emitted after 86 logical blocks.
 * It improves sync scoring for late stations without delaying primary decode. */
#define RX_SLOT_FRAMER_REFINE_BLOCKS 86u

typedef enum {
    RX_SLOT_FRAMER_OK = 0,
    RX_SLOT_FRAMER_ERR_INVALID = -1,
    RX_SLOT_FRAMER_ERR_NOT_INITIALIZED = -2,
    RX_SLOT_FRAMER_ERR_STATE = -3,
    RX_SLOT_FRAMER_ERR_SINK = -4,
    RX_SLOT_FRAMER_ERR_RANGE = -5
} RxSlotFramerStatus;

typedef enum {
    /* Latch the beginning of the currently filling 960-sample block as the
     * logical decode origin for this UTC slot. */
    RX_SLOT_FRAMER_EVENT_BEGIN_WINDOW = 0,

    /* One continuous 960-sample engine block. Blocks never reset at UTC slot
     * boundaries; a block may straddle two adjacent 15-second slots. */
    RX_SLOT_FRAMER_EVENT_ENGINE_BLOCK = 1,

    /* Historical name retained for compatibility: primary search ready. */
    RX_SLOT_FRAMER_EVENT_FINALIZE_WINDOW = 2,

    /* Second candidate-only search ready. */
    RX_SLOT_FRAMER_EVENT_REFINE_WINDOW = 3,

    /* Real stream discontinuity: discard partial block/DSP continuity. */
    RX_SLOT_FRAMER_EVENT_STREAM_RESET = 4
} RxSlotFramerEventType;

typedef struct {
    RxSlotFramerEventType type;
    int64_t slot_id;

    /* Valid only for ENGINE_BLOCK and only for the duration of the callback. */
    const float *samples;
    size_t sample_count;
} RxSlotFramerEvent;

/* Return 0 when the event was accepted. Any non-zero result faults the framer. */
typedef int (*RxSlotFramerEmitFn)(void *ctx,
                                  const RxSlotFramerEvent *event);

typedef struct {
    int initialized;
    int faulted;

    int64_t slot_id;
    uint32_t sample_offset;

    /* The blockizer runs continuously even before the first usable slot
     * boundary. A non-zero initial sample_offset therefore fills history but
     * does not create a decode anchor until the next UTC boundary. */
    int slot_anchor_valid;
    int begin_pending;

    uint32_t slot_block_count;
    int primary_emitted;
    int refine_emitted;

    size_t block_fill;
    float block[RX_SLOT_FRAMER_BLOCK_SAMPLES];
} RxSlotFramer;

/*
 * Establish the UTC timing reference for the first input sample.
 *
 * If sample_offset is zero, the next process call emits BEGIN_WINDOW before
 * consuming data. If non-zero, audio is still blockized continuously, but the
 * first decode anchor is latched only at the next UTC slot boundary.
 */
RxSlotFramerStatus rx_slot_framer_init(RxSlotFramer *framer,
                                       int64_t slot_id,
                                       uint32_t sample_offset);

void rx_slot_framer_destroy(RxSlotFramer *framer);

/*
 * Consume a continuous 6 kHz mono-float stream. Input chunk boundaries have no
 * framing meaning. The 960-sample blockizer is independent of UTC boundaries;
 * the former 720-sample slot remainder is never discarded.
 *
 * BEGIN_WINDOW latches a slot anchor, FINALIZE_WINDOW follows the 79th completed
 * block relative to that anchor, and REFINE_WINDOW follows the 86th.
 */
RxSlotFramerStatus rx_slot_framer_process(RxSlotFramer *framer,
                                          const float *samples,
                                          size_t sample_count,
                                          RxSlotFramerEmitFn emit,
                                          void *emit_ctx);

/*
 * Declare a real stream discontinuity and establish a new UTC reference.
 * Partial block state is discarded and STREAM_RESET is emitted. Continuous
 * slot-boundary operation never calls this function.
 */
RxSlotFramerStatus rx_slot_framer_reset_stream(RxSlotFramer *framer,
                                               int64_t slot_id,
                                               uint32_t sample_offset,
                                               RxSlotFramerEmitFn emit,
                                               void *emit_ctx);

#ifdef __cplusplus
}
#endif

#endif

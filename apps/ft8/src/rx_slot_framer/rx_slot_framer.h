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
    RX_SLOT_FRAMER_EVENT_STREAM_RESET = 3
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

    /* A non-zero initial offset means the first partial slot is discarded. */
    int waiting_for_full_boundary;
    int window_active;

    size_t block_fill;
    float block[RX_SLOT_FRAMER_BLOCK_SAMPLES];
} RxSlotFramer;

/*
 * Establish the timing reference for the first input sample.
 *
 * slot_id identifies the FT8 15-second UTC slot containing that sample.
 * sample_offset is the sample position inside that slot, 0..89999.
 * If sample_offset is non-zero, the first partial slot is discarded and the
 * first BEGIN_WINDOW is emitted only at the next complete slot boundary.
 */
RxSlotFramerStatus rx_slot_framer_init(RxSlotFramer *framer,
                                       int64_t slot_id,
                                       uint32_t sample_offset);

void rx_slot_framer_destroy(RxSlotFramer *framer);

/*
 * Consume a continuous 6 kHz mono-float stream. Input chunk boundaries have no
 * framing meaning. The framer emits complete 960-sample engine blocks only.
 * The 720-sample remainder at a 15-second slot boundary is discarded rather
 * than carried into the next slot.
 */
RxSlotFramerStatus rx_slot_framer_process(RxSlotFramer *framer,
                                          const float *samples,
                                          size_t sample_count,
                                          RxSlotFramerEmitFn emit,
                                          void *emit_ctx);

/*
 * Declare a stream discontinuity and establish a new timing reference.
 * Partial block/window state is discarded and STREAM_RESET is emitted so the
 * downstream Ft8Engine can clear DSP continuity while preserving protocol
 * knowledge. As at init, a non-zero offset suppresses the first partial slot.
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

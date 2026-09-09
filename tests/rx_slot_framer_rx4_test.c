#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rx_slot_framer.h"

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        return 1; \
    } \
} while (0)

typedef struct {
    size_t begin_count;
    size_t block_count;
    size_t finalize_count;
    size_t reset_count;
    int64_t begin_slots[8];
    int64_t finalize_slots[8];
    int64_t reset_slots[8];
    uint64_t sample_hash;
    size_t block_samples;
    int fail_on_next_block;
} EventLog;

static uint64_t fnv1a_u32(uint64_t hash, uint32_t value)
{
    for (unsigned shift = 0u; shift < 32u; shift += 8u) {
        hash ^= (uint8_t)(value >> shift);
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static int log_event(void *ctx, const RxSlotFramerEvent *event)
{
    EventLog *log = (EventLog *)ctx;

    if (!log || !event)
        return -1;

    switch (event->type) {
    case RX_SLOT_FRAMER_EVENT_BEGIN_WINDOW:
        if (log->begin_count < 8u)
            log->begin_slots[log->begin_count] = event->slot_id;
        log->begin_count++;
        if (event->samples != NULL || event->sample_count != 0u)
            return -1;
        break;

    case RX_SLOT_FRAMER_EVENT_ENGINE_BLOCK:
        if (log->fail_on_next_block) {
            log->fail_on_next_block = 0;
            return -1;
        }
        if (!event->samples || event->sample_count != RX_SLOT_FRAMER_BLOCK_SAMPLES)
            return -1;
        for (size_t i = 0u; i < event->sample_count; ++i) {
            uint32_t bits;
            memcpy(&bits, &event->samples[i], sizeof(bits));
            log->sample_hash = fnv1a_u32(log->sample_hash, bits);
        }
        log->block_samples += event->sample_count;
        log->block_count++;
        break;

    case RX_SLOT_FRAMER_EVENT_FINALIZE_WINDOW:
        if (log->finalize_count < 8u)
            log->finalize_slots[log->finalize_count] = event->slot_id;
        log->finalize_count++;
        if (event->samples != NULL || event->sample_count != 0u)
            return -1;
        break;

    case RX_SLOT_FRAMER_EVENT_STREAM_RESET:
        if (log->reset_count < 8u)
            log->reset_slots[log->reset_count] = event->slot_id;
        log->reset_count++;
        if (event->samples != NULL || event->sample_count != 0u)
            return -1;
        break;

    default:
        return -1;
    }

    return 0;
}

static void init_log(EventLog *log)
{
    memset(log, 0, sizeof(*log));
    log->sample_hash = UINT64_C(1469598103934665603);
}

static int feed_generated(RxSlotFramer *framer,
                          size_t total_samples,
                          size_t chunk_size,
                          uint64_t sequence_start,
                          EventLog *log)
{
    float buffer[1024];
    size_t done = 0u;

    if (chunk_size == 0u || chunk_size > 1024u)
        return -1;

    while (done < total_samples) {
        size_t n = total_samples - done;
        if (n > chunk_size)
            n = chunk_size;

        for (size_t i = 0u; i < n; ++i)
            buffer[i] = (float)(sequence_start + done + i);

        if (rx_slot_framer_process(framer, buffer, n, log_event, log) != RX_SLOT_FRAMER_OK)
            return -1;
        done += n;
    }

    return 0;
}

static int test_invalid_and_empty(void)
{
    RxSlotFramer framer;
    EventLog log;

    init_log(&log);
    CHECK(rx_slot_framer_init(NULL, 0, 0u) == RX_SLOT_FRAMER_ERR_INVALID);
    CHECK(rx_slot_framer_init(&framer, 0, RX_SLOT_FRAMER_SLOT_SAMPLES) ==
          RX_SLOT_FRAMER_ERR_INVALID);
    CHECK(rx_slot_framer_init(&framer, 7, 0u) == RX_SLOT_FRAMER_OK);
    CHECK(rx_slot_framer_process(&framer, NULL, 0u, NULL, NULL) == RX_SLOT_FRAMER_OK);
    CHECK(log.begin_count == 0u);
    rx_slot_framer_destroy(&framer);
    CHECK(rx_slot_framer_process(&framer, NULL, 0u, NULL, NULL) ==
          RX_SLOT_FRAMER_ERR_NOT_INITIALIZED);
    return 0;
}

static int test_exact_slot_and_remainder(void)
{
    RxSlotFramer framer;
    EventLog log;

    init_log(&log);
    CHECK(rx_slot_framer_init(&framer, 7, 0u) == RX_SLOT_FRAMER_OK);
    CHECK(feed_generated(&framer, RX_SLOT_FRAMER_SLOT_SAMPLES, 257u, 0u, &log) == 0);

    CHECK(log.begin_count == 1u);
    CHECK(log.begin_slots[0] == 7);
    CHECK(log.block_count == 93u);
    CHECK(log.block_samples == 93u * RX_SLOT_FRAMER_BLOCK_SAMPLES);
    CHECK(log.finalize_count == 1u);
    CHECK(log.finalize_slots[0] == 7);
    CHECK(log.reset_count == 0u);

    CHECK(framer.slot_id == 8);
    CHECK(framer.sample_offset == 0u);
    CHECK(framer.block_fill == 0u);
    CHECK(framer.window_active == 0);
    CHECK(framer.waiting_for_full_boundary == 0);
    return 0;
}

static int test_partial_first_slot(void)
{
    RxSlotFramer framer;
    EventLog log;
    float one = 1.0f;
    float rest[959];

    memset(rest, 0, sizeof(rest));
    init_log(&log);
    CHECK(rx_slot_framer_init(&framer, 10, 30000u) == RX_SLOT_FRAMER_OK);
    CHECK(feed_generated(&framer, 60000u, 311u, 0u, &log) == 0);
    CHECK(log.begin_count == 0u);
    CHECK(log.block_count == 0u);
    CHECK(log.finalize_count == 0u);
    CHECK(framer.slot_id == 11);
    CHECK(framer.sample_offset == 0u);

    CHECK(rx_slot_framer_process(&framer, &one, 1u, log_event, &log) == RX_SLOT_FRAMER_OK);
    CHECK(log.begin_count == 1u);
    CHECK(log.begin_slots[0] == 11);
    CHECK(log.block_count == 0u);

    CHECK(rx_slot_framer_process(&framer, rest, 959u, log_event, &log) == RX_SLOT_FRAMER_OK);
    CHECK(log.block_count == 1u);
    return 0;
}

static int test_chunk_invariance_and_multi_slot(void)
{
    RxSlotFramer a;
    RxSlotFramer b;
    EventLog log_a;
    EventLog log_b;

    init_log(&log_a);
    init_log(&log_b);
    CHECK(rx_slot_framer_init(&a, 100, 0u) == RX_SLOT_FRAMER_OK);
    CHECK(rx_slot_framer_init(&b, 100, 0u) == RX_SLOT_FRAMER_OK);

    CHECK(feed_generated(&a, 2u * RX_SLOT_FRAMER_SLOT_SAMPLES, 997u, 0u, &log_a) == 0);
    CHECK(feed_generated(&b, 2u * RX_SLOT_FRAMER_SLOT_SAMPLES, 13u, 0u, &log_b) == 0);

    CHECK(log_a.begin_count == 2u);
    CHECK(log_a.block_count == 186u);
    CHECK(log_a.finalize_count == 2u);
    CHECK(log_a.begin_slots[0] == 100 && log_a.begin_slots[1] == 101);
    CHECK(log_a.finalize_slots[0] == 100 && log_a.finalize_slots[1] == 101);

    CHECK(log_b.begin_count == log_a.begin_count);
    CHECK(log_b.block_count == log_a.block_count);
    CHECK(log_b.finalize_count == log_a.finalize_count);
    CHECK(log_b.block_samples == log_a.block_samples);
    CHECK(log_b.sample_hash == log_a.sample_hash);
    CHECK(b.slot_id == 102 && b.sample_offset == 0u);
    return 0;
}

static int test_stream_reset(void)
{
    RxSlotFramer framer;
    EventLog log;
    float one = 5.0f;

    init_log(&log);
    CHECK(rx_slot_framer_init(&framer, 1, 0u) == RX_SLOT_FRAMER_OK);
    CHECK(feed_generated(&framer, 2000u, 503u, 0u, &log) == 0);
    CHECK(log.begin_count == 1u);
    CHECK(log.block_count == 2u);
    CHECK(framer.block_fill == 80u);

    CHECK(rx_slot_framer_reset_stream(&framer, 20, 100u, log_event, &log) ==
          RX_SLOT_FRAMER_OK);
    CHECK(log.reset_count == 1u);
    CHECK(log.reset_slots[0] == 20);
    CHECK(framer.block_fill == 0u);
    CHECK(framer.window_active == 0);
    CHECK(framer.waiting_for_full_boundary == 1);

    CHECK(feed_generated(&framer, RX_SLOT_FRAMER_SLOT_SAMPLES - 100u,
                         701u, 0u, &log) == 0);
    CHECK(log.begin_count == 1u);
    CHECK(framer.slot_id == 21 && framer.sample_offset == 0u);

    CHECK(rx_slot_framer_process(&framer, &one, 1u, log_event, &log) == RX_SLOT_FRAMER_OK);
    CHECK(log.begin_count == 2u);
    CHECK(log.begin_slots[1] == 21);
    return 0;
}

static int test_sink_failure_requires_reset(void)
{
    RxSlotFramer framer;
    EventLog log;
    float block[RX_SLOT_FRAMER_BLOCK_SAMPLES];

    memset(block, 0, sizeof(block));
    init_log(&log);
    CHECK(rx_slot_framer_init(&framer, 3, 0u) == RX_SLOT_FRAMER_OK);

    log.fail_on_next_block = 1;
    CHECK(rx_slot_framer_process(&framer, block, RX_SLOT_FRAMER_BLOCK_SAMPLES,
                                 log_event, &log) == RX_SLOT_FRAMER_ERR_SINK);
    CHECK(framer.faulted == 1);
    CHECK(rx_slot_framer_process(&framer, block, 1u, log_event, &log) ==
          RX_SLOT_FRAMER_ERR_STATE);

    CHECK(rx_slot_framer_reset_stream(&framer, 4, 0u, log_event, &log) ==
          RX_SLOT_FRAMER_OK);
    CHECK(framer.faulted == 0);
    CHECK(rx_slot_framer_process(&framer, block, RX_SLOT_FRAMER_BLOCK_SAMPLES,
                                 log_event, &log) == RX_SLOT_FRAMER_OK);
    CHECK(log.block_count == 1u);
    return 0;
}

int main(void)
{
    if (test_invalid_and_empty() != 0 ||
        test_exact_slot_and_remainder() != 0 ||
        test_partial_first_slot() != 0 ||
        test_chunk_invariance_and_multi_slot() != 0 ||
        test_stream_reset() != 0 ||
        test_sink_failure_requires_reset() != 0)
        return 1;

    puts("rx_slot_framer_rx4_test: PASS");
    return 0;
}

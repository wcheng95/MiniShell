#ifndef JS8_SLOT_H
#define JS8_SLOT_H
#include <stddef.h>
#include <stdint.h>
#define JS8_LIVE_SLOT_SAMPLES 90000u
#define JS8_LIVE_WINDOW_SAMPLES 89280u
typedef enum { JS8_SLOT_BEGIN, JS8_SLOT_SAMPLES, JS8_SLOT_READY, JS8_SLOT_DROP } Js8SlotEvent;
typedef int (*Js8SlotSink)(void *, Js8SlotEvent, uint32_t, const float *, size_t);
typedef struct {
    int scheduled, active;
    uint32_t capture_slot, captured;
    uint64_t next_slot;
} Js8SlotScheduler;
/* Each call supplies a fresh UTC-derived FIRST sample position. Counting within
 * an active capture is continuous; each pre-roll start is located in timed input.
 * Use the first available sample on overshoot; never pad or synthesize. Reset with zero. */
int js8_slot_feed(Js8SlotScheduler *, uint32_t slot, uint32_t offset,
                   const float *, size_t, Js8SlotSink, void *);
#endif

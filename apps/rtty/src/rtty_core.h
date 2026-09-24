#ifndef RTTY_CORE_H
#define RTTY_CORE_H
#include <stddef.h>
#include <stdint.h>
#define RTTY_SAMPLE_RATE 12000
#define RTTY_SAMPLES_PER_BIT (12000.0 / 45.45)
typedef void (*RttyCharacter)(void *context, char character);
typedef struct {
    int16_t acquisition[1200], history[120];
    unsigned fill, head, tick, locked, previous, framing, stage, code, since_valid;
    uint8_t figures;
    double remaining;
    float mark_hz, coefficient[2];
} RttyCore;
void rtty_core_init(RttyCore *core);
/* Fixed 12 kHz S16 mono; callbacks are synchronous. Arbitrary chunk sizes. */
void rtty_core_process(RttyCore *core, const int16_t *pcm, size_t count,
                       RttyCharacter emit, void *context);
#endif

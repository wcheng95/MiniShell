#include "adv_audio_uac_buffer.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%d: %s\n", __LINE__, #x); exit(1); } } while (0)
static adv_uac_buffer_t ring;
static void encode(uint8_t *p, int32_t value)
{
    uint32_t bits = (uint32_t)value;
    for (unsigned i = 0; i < 3; ++i) p[i] = (uint8_t)(bits >> (i * 8));
}
int main(void)
{
    const int32_t input[] = {0, 1, 255, 256, 8388607, -8388608, -1, -256, -257};
    const int16_t output[] = {0, 0, 0, 1, 32767, -32768, -1, -1, -2};
    uint8_t bytes[9 * 4 * 6];
    for (unsigned i = 0; i < 9; ++i) {
        for (unsigned phase = 0; phase < 4; ++phase) {
            uint8_t *p = bytes + (i * 4 + phase) * 6;
            encode(p, phase ? 99999 : input[i]);
            encode(p + 3, phase ? 22222 : input[8 - i]);
        }
    }
    for (unsigned chunk = 1; chunk <= 29; ++chunk) {
        memset(&ring, 0, sizeof(ring));
        for (unsigned pos = 0; pos < sizeof(bytes);) {
            unsigned size = sizeof(bytes) - pos;
            if (size > chunk) size = chunk;
            CHECK(adv_uac_feed(&ring, adv_uac_begin(&ring), bytes + pos, size));
            pos += size;
        }
        int16_t data[18];
        CHECK(adv_uac_take(&ring, data, 3) == 3);
        CHECK(adv_uac_take(&ring, data + 6, 9) == 6);
        for (unsigned i = 0; i < 9; ++i) {
            CHECK(data[2 * i] == output[i]);
            CHECK(data[2 * i + 1] == output[8 - i]);
        }
    }
    memset(&ring, 0, sizeof(ring));
    ring.head = ring.tail = UINT32_MAX - 3u;
    CHECK(adv_uac_feed(&ring, adv_uac_begin(&ring), bytes, sizeof(bytes)));
    int16_t data[18];
    CHECK(adv_uac_take(&ring, data, 9) == 9);
    for (unsigned i = 0; i < 9; ++i) CHECK(data[2 * i] == output[i]);
    memset(&ring, 0, sizeof(ring));
    uint8_t frame[24] = {0};
    for (unsigned i = 0; i < ADV_UAC_RING_FRAMES; ++i)
        CHECK(adv_uac_feed(&ring, adv_uac_begin(&ring), frame, sizeof(frame)));
    CHECK(ring.high_water == ADV_UAC_RING_FRAMES);
    CHECK(!adv_uac_feed(&ring, adv_uac_begin(&ring), frame, sizeof(frame)));
    CHECK(ring.overflows == 1 && ring.pending && ring.head == ring.tail);
    CHECK(adv_uac_take(&ring, data, 9) == 0);
    adv_uac_ticket_t pending_read = adv_uac_begin(&ring);
    CHECK(adv_uac_ack(&ring));
    ring.reset_required = false; // native stop/start completed
    CHECK(adv_uac_feed(&ring, pending_read, bytes, sizeof(bytes)));
    CHECK(adv_uac_take(&ring, data, 9) == 0);
    adv_uac_ticket_t inflight = adv_uac_begin(&ring);
    adv_uac_loss(&ring); // new error during an in-flight read is never lost
    CHECK(adv_uac_ack(&ring));
    ring.reset_required = false;
    CHECK(adv_uac_feed(&ring, inflight, bytes, sizeof(bytes)));
    CHECK(adv_uac_take(&ring, data, 9) == 0);
    adv_uac_loss(&ring);
    pending_read = adv_uac_begin(&ring);
    CHECK(adv_uac_ack(&ring));
    adv_uac_loss(&ring); // second loss after first ACK must be seen again
    CHECK(ring.pending && adv_uac_ack(&ring));
    CHECK(!adv_uac_ack(&ring));
    CHECK(adv_uac_feed(&ring, adv_uac_begin(&ring), bytes, sizeof(bytes)));
    CHECK(adv_uac_take(&ring, data, 9) == 0); // reset still required
    ring.reset_required = false;
    CHECK(adv_uac_feed(&ring, pending_read, bytes, sizeof(bytes)));
    CHECK(ring.head == ring.tail);
    CHECK(adv_uac_feed(&ring, adv_uac_begin(&ring), bytes, sizeof(bytes)));
    CHECK(adv_uac_take(&ring, data, 9) == 9);
    puts("ADV UAC conversion/ring/epoch: PASS");
    return 0;
}

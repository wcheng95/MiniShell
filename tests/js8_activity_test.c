#include "js8_activity.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    struct { uint64_t before; Js8Activity event; uint64_t after; } g;
    memset(&g, 0xa5, sizeof(g));
    Js8ActivityFields f = {0};
    char bytes[1024];
    for (unsigned i = 0; i < sizeof(bytes); ++i) bytes[i] = (char)(i % 256);
    for (int kind = JS8_ACTIVITY_HEARTBEAT; kind <= JS8_ACTIVITY_MESSAGE; ++kind) {
        f.kind = (Js8ActivityKind)kind; f.slot_index = UINT32_MAX;
        f.last_slot = UINT32_MAX; f.first_slot = 0;
        f.frequency_millihz = INT32_MIN; f.tx_flags = 7; f.score = -10; f.hard_errors = 15;
        strcpy(f.call, "F/AG6AQ/P"); strcpy(f.from, "AG6AQ"); strcpy(f.to, "K1ABC/P");
        strcpy(f.grid, "CM97"); strcpy(f.beacon, "CQ FIELD"); strcpy(f.command, " ACK");
        f.command_code = 14; f.ack = 1; f.subtype = 3; f.bits3 = 7; f.extra = 65535;
        size_t length = kind == JS8_ACTIVITY_DATA ? JS8_JSC_TEXT_SIZE-1 :
                        kind == JS8_ACTIVITY_MESSAGE ? 1023 : 0;
        assert(js8_activity_build(&f, bytes, length, &g.event) == 0);
        assert(!memcmp(&g.event.fields, &f, sizeof(f)));
        assert(g.event.elapsed_seconds == UINT64_C(64424509425));
        assert(g.event.text_len == length && !memcmp(g.event.text, bytes, length));
        assert(g.event.text[length] == 0);
        Js8Activity saved = g.event;
        assert(js8_activity_build(&f, bytes, length+1, &g.event) == -1);
        assert(!memcmp(&saved, &g.event, sizeof(saved)));
    }
    f.kind = JS8_ACTIVITY_DATA;
    for (int codec = JS8_ACTIVITY_CODEC_NONE; codec <= JS8_ACTIVITY_CODEC_JSC; ++codec) {
        f.codec = (Js8ActivityCodec)codec;
        size_t max = codec == JS8_ACTIVITY_CODEC_HUFFMAN ? 34 : 383;
        assert(!js8_activity_build(&f, bytes, max, &g.event));
        assert(js8_activity_build(&f, bytes, max+1, &g.event) == -1);
        assert(!js8_activity_build(&f, NULL, 0, &g.event));
    }
    Js8Activity saved = g.event;
#define INVALID() do { assert(js8_activity_build(&f, NULL, 0, &g.event) == -1); \
                       assert(!memcmp(&saved, &g.event, sizeof(saved))); } while (0)
    f.codec = (Js8ActivityCodec)3; INVALID();
    f.kind = (Js8ActivityKind)99; INVALID();
    f.kind = JS8_ACTIVITY_DIRECTED; memset(f.from, 'X', sizeof(f.from)); INVALID();
    memset(f.from, 0, sizeof(f.from)); f.command_code = 32; INVALID();
    f.command_code = 14; f.ack = 2; INVALID(); f.ack = 1;
    f.kind = JS8_ACTIVITY_CQ; f.subtype = 8; INVALID(); f.subtype = 7;
    memset(f.grid, 'X', sizeof(f.grid)); INVALID(); memset(f.grid, 0, sizeof(f.grid));
    f.kind = JS8_ACTIVITY_COMPOUND; f.bits3 = 8; INVALID(); f.bits3 = 7;
    f.compound_directed = 2; INVALID(); f.compound_directed = 1;
    f.kind = JS8_ACTIVITY_MESSAGE; f.last_slot = 0; INVALID();
    f.last_slot = UINT32_MAX; f.tx_flags = 8; INVALID(); f.tx_flags = 0;
    f.hard_errors = -1; INVALID(); f.hard_errors = 0;
    assert(js8_activity_build(NULL, NULL, 0, &g.event) == -1);
    assert(js8_activity_build(&f, NULL, 1, &g.event) == -1);
    assert(js8_activity_build(&f, NULL, 0, NULL) == -1);
    assert(!memcmp(&saved, &g.event, sizeof(saved)));
    assert(g.before == UINT64_C(0xa5a5a5a5a5a5a5a5) && g.after == g.before);
    printf("js8_activity_test: PASS event=%zu fields=%zu bytes\n", sizeof(Js8Activity), sizeof(f));
}

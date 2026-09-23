#include "js8_reassembly.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

static Js8RxFragment header(uint32_t slot, int32_t frequency, uint8_t flags)
{
    Js8RxFragment e = {0};
    e.kind = JS8_RX_FRAGMENT_DIRECTED;
    e.slot_index = slot; e.frequency_millihz = frequency; e.tx_flags = flags;
    strcpy(e.from, "AG6AQ"); strcpy(e.to, "K1ABC"); e.command_code = 31;
    return e;
}
static Js8RxFragment data(uint32_t slot, int32_t frequency, uint8_t flags, const char *text)
{
    Js8RxFragment e = {0};
    e.kind = JS8_RX_FRAGMENT_DATA;
    e.slot_index = slot; e.frequency_millihz = frequency; e.tx_flags = flags;
    e.text = text; e.text_len = text ? (uint16_t)strlen(text) : 0;
    return e;
}
static unsigned active(const Js8RxReassembly *s)
{
    unsigned n = 0;
    for (int i = 0; i < JS8_RX_CONTEXTS; ++i) n += s->contexts[i].active;
    return n;
}
int main(void)
{
    struct { uint64_t before; Js8RxReassembly s; uint64_t after; } guarded = {0};
    struct { uint64_t before; Js8RxMessage m; uint64_t after; } output = {0};
    guarded.before = guarded.after = output.before = output.after = UINT64_C(0x123456789abcdef0);
    Js8RxReassembly *s = &guarded.s;
    Js8RxMessage *m = &output.m;
    Js8RxDrops drops;
    Js8RxFragment e;
#define FEED(expected) assert(js8_rx_reassembly_feed(s, &e, m, &drops) == (expected))
    js8_rx_reassembly_init(s);
    e = data(0, 700000, 2, "orphan"); FEED(JS8_RX_ORPHAN);
    e = header(0, 700000, 1); FEED(JS8_RX_ACCEPTED);
    e = data(1, 706250, 0, "HELLO "); FEED(JS8_RX_ACCEPTED);
    Js8RxReassembly saved = *s;
    e = data(1, 707000, 2, "DUPLICATE"); FEED(JS8_RX_DUPLICATE);
    assert(!memcmp(&saved, s, sizeof(saved)));
    e.slot_index = 0; FEED(JS8_RX_DUPLICATE);
    e = data(2, 716251, 2, "too far"); FEED(JS8_RX_ORPHAN);
    assert(!memcmp(&saved, s, sizeof(saved)));
    e = data(2, 696875, 6, "WORLD"); FEED(JS8_RX_COMPLETE);
    assert(!strcmp(m->text, "HELLO WORLD") && m->text_len == 11);
    assert(m->frequency_millihz == 696875 && m->first_slot == 0 && m->last_slot == 2);
    assert(!strcmp(m->from, "AG6AQ") && !strcmp(m->to, "K1ABC") && !active(s));
    e = header(5, 0, 3); FEED(JS8_RX_COMPLETE);
    assert(!m->text_len && !m->text[0] && m->first_slot == 5 && m->last_slot == 5);
    for (unsigned cmd = 0; cmd < 32; ++cmd) {
        e = header(5, 0, 1); e.command_code = (uint8_t)cmd;
        FEED(cmd == 31 ? JS8_RX_ACCEPTED : JS8_RX_IGNORED);
        assert(active(s) == (cmd == 31));
    }
    js8_rx_reassembly_init(s);
    e = header(5, 0, 2); FEED(JS8_RX_IGNORED);
    e.tx_flags = 1; strcpy(e.from, "<....>"); FEED(JS8_RX_IGNORED);
    strcpy(e.from, "AG6AQ"); strcpy(e.to, "<....>"); FEED(JS8_RX_IGNORED);
    assert(!active(s));

    /* A gap persists through subsequent contiguous data and never emits. */
    e = header(0, 100000, 1); FEED(JS8_RX_ACCEPTED);
    e = data(2, 100000, 0, "missing"); FEED(JS8_RX_GAP);
    Js8RxMessage untouched = *m;
    e = data(3, 100000, 2, " end"); FEED(JS8_RX_GAP);
    assert(!active(s) && !memcmp(m, &untouched, sizeof(*m)));
    e = header(0, 100000, 1); FEED(JS8_RX_ACCEPTED);
    e = data(6, 100000, 0, "90 seconds"); FEED(JS8_RX_GAP);
    assert(!drops.expired && active(s) == 1);
    e = data(13, 100000, 2, "expired"); FEED(JS8_RX_ORPHAN);
    assert(drops.expired == 1 && !active(s));
    /* Ignored commands still trigger cleanup; silence alone cannot complete. */
    e = header(0, 100000, 1); FEED(JS8_RX_ACCEPTED);
    e = header(7, 200000, 1); e.command_code = 14; FEED(JS8_RX_IGNORED);
    assert(drops.expired == 1 && !active(s));

    /* Four concurrent streams, interleaved event order, distinct addresses. */
    const char *calls[] = {"W6AAA", "K6BBB", "N7CCC", "W7DDD"};
    for (int i = 0; i < 4; ++i) {
        e = header(0, 600000 + i*150000, 1); strcpy(e.from, calls[i]);
        FEED(JS8_RX_ACCEPTED);
    }
    assert(active(s) == 4);
    for (int i = 3; i >= 0; --i) {
        e = data(1, 606250 + i*150000, 0, calls[i]); FEED(JS8_RX_ACCEPTED);
    }
    for (int i = 0; i < 4; ++i) {
        e = data(2, 600000 + i*150000, 2, "!"); FEED(JS8_RX_COMPLETE);
        assert(!strcmp(m->from, calls[i]) && !memcmp(m->text, calls[i], 5) && m->text[5] == '!');
    }
    assert(!active(s));
    /* FIRST within 10 Hz replaces; nearest wins, then lowest stable index. */
    e = header(0, 700000, 1); FEED(JS8_RX_ACCEPTED);
    e = header(0, 718750, 1); FEED(JS8_RX_ACCEPTED);
    e = data(1, 710000, 0, "near second"); FEED(JS8_RX_ACCEPTED);
    assert(s->contexts[1].message.text_len == 11 && !s->contexts[0].message.text_len);
    e = data(2, 705000, 0, "tie first"); FEED(JS8_RX_GAP);
    assert(s->contexts[0].message.text_len == 9 && s->contexts[0].message.frequency_millihz == 705000);
    e = header(3, 695000, 1); FEED(JS8_RX_ACCEPTED);
    assert(drops.replaced == 1 && !s->contexts[0].gap && !s->contexts[0].message.text_len);
    e = data(4, 685000, 2, "edge"); FEED(JS8_RX_COMPLETE);
    assert(!strcmp(m->text, "edge") && m->first_slot == 3);

    js8_rx_reassembly_init(s);
    for (int i = 0; i < 4; ++i) {
        e = header(0, i*100000, 1); FEED(JS8_RX_ACCEPTED);
    }
    e = data(1, 0, 0, "newer"); FEED(JS8_RX_ACCEPTED);
    e = header(2, 500000, 1); FEED(JS8_RX_ACCEPTED);
    assert(drops.evicted == 2 && active(s) == 4); /* oldest tied index 1 */
    e = header(3, 505000, 3); FEED(JS8_RX_COMPLETE);
    assert(drops.replaced == 2 && active(s) == 3 && !m->text_len);

    /* Full capacity includes the terminator; no silent truncation. */
    js8_rx_reassembly_init(s);
    char text[1025]; memset(text, 'x', sizeof(text)); text[1024] = 0;
    e = header(0, 0, 1); FEED(JS8_RX_ACCEPTED);
    e = data(1, 0, 0, text); e.text_len = 1023; FEED(JS8_RX_ACCEPTED);
    assert(s->contexts[0].message.text[1023] == 0);
    e = data(2, 0, 2, NULL); FEED(JS8_RX_COMPLETE);
    assert(m->text_len == 1023 && m->text[1023] == 0);
    e = header(0, 0, 1); FEED(JS8_RX_ACCEPTED);
    e = data(1, 0, 2, text); FEED(JS8_RX_OVERFLOW);
    assert(!active(s) && m->text_len == 1023);
    e = header(0, 0, 1); FEED(JS8_RX_ACCEPTED);
    e = data(1, 0, 0, text); e.text_len = 1023; FEED(JS8_RX_ACCEPTED);
    e = data(2, 0, 2, "x"); FEED(JS8_RX_OVERFLOW);
    assert(!active(s));
    e = header(UINT32_MAX-1, INT32_MIN, 1); FEED(JS8_RX_ACCEPTED);
    e = data(UINT32_MAX, INT32_MAX, 2, "far"); FEED(JS8_RX_ORPHAN);
    e.frequency_millihz = INT32_MIN + 10000; FEED(JS8_RX_COMPLETE);
    e = header(UINT32_MAX, INT32_MAX, 1); FEED(JS8_RX_ACCEPTED);
    e = data(0, INT32_MAX, 2, "no wrap"); FEED(JS8_RX_DUPLICATE);
    js8_rx_reassembly_init(s);
    /* Length, not NUL, defines decoded bytes. */
    e = header(0, 0, 1); FEED(JS8_RX_ACCEPTED);
    e = data(1, 0, 2, "a\0b"); e.text_len = 3; FEED(JS8_RX_COMPLETE);
    assert(m->text_len == 3 && !memcmp(m->text, "a\0b\0", 4));

    saved = *s; untouched = *m;
    memset(&drops, 0xa5, sizeof(drops)); Js8RxDrops old_drops = drops;
    e = data(0, 0, 0, NULL); e.text_len = 1; FEED(JS8_RX_INVALID);
    e = header(0, 0, 1); memset(e.from, 'x', sizeof(e.from)); FEED(JS8_RX_INVALID);
    e = header(0, 0, 1); e.to[0] = 0; FEED(JS8_RX_INVALID);
    e = header(0, 0, 1); e.command_code = 32; FEED(JS8_RX_INVALID);
    e = header(0, 0, 8); FEED(JS8_RX_INVALID);
    e = data(0, 0, 0, NULL); e.kind = (Js8RxFragmentKind)99; FEED(JS8_RX_INVALID);
    e = header(0, 0, 1);
    assert(js8_rx_reassembly_feed(NULL, &e, m, &drops) == JS8_RX_INVALID);
    assert(js8_rx_reassembly_feed(s, NULL, m, &drops) == JS8_RX_INVALID);
    assert(js8_rx_reassembly_feed(s, &e, NULL, &drops) == JS8_RX_INVALID);
    assert(js8_rx_reassembly_feed(s, &e, m, NULL) == JS8_RX_INVALID);
    assert(!memcmp(&saved, s, sizeof(saved)) && !memcmp(&untouched, m, sizeof(*m)));
    assert(!memcmp(&drops, &old_drops, sizeof(drops)));
    assert(guarded.before == UINT64_C(0x123456789abcdef0) && guarded.after == guarded.before);
    assert(output.before == guarded.before && output.after == guarded.before);
    printf("js8_reassembly_test: PASS state=%zu context=%zu message=%zu bytes\n", sizeof(*s), sizeof(s->contexts[0]), sizeof(*m));
    return 0;
}

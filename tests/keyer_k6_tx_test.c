#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "tx_engine.h"
#include "config_service.h"

static keyer_config_t c;
static tx_engine_t t;
static void reset(void) { config_service_defaults(&c); c.tx_delay_s = 0; tx_engine_init(&t); }
static bool step(uint64_t now) { return tx_engine_step(&t, &c, now, false); }
static void timing(void)
{
    reset();
    assert(tx_engine_append(&t, "ae t", 0) == TX_OK);
    assert(step(0)); assert(!strcmp(t.fifo, "E T"));
    assert(step(59999)); assert(!step(60000)); /* A dit */
    assert(!step(119999)); assert(step(120000)); /* element gap */
    assert(step(299999)); assert(!step(300000)); /* A dah */
    assert(!step(479999)); assert(step(480000)); /* character gap */
    assert(!step(540000));
    assert(!step(720000)); assert(!step(959999)); assert(step(960000)); /* word=7 */
    assert(step(1139999)); assert(!step(1140000));
    assert(!step(1320000)); assert(t.phase == TX_IDLE && !t.count);
    /* New WPM affects subsequent elements, never truncates a keyed element. */
    reset(); assert(tx_engine_append(&t, "i", 0) == TX_OK); assert(step(0));
    c.wpm = 60; assert(step(59999)); assert(!step(60000)); assert(step(80000));
    assert(!step(100000));
}
static void pending_repeat_cancel(void)
{
    for (unsigned action = 0; action < 4; ++action) {
        reset(); strcpy(c.messages[0], "E");
        assert(tx_engine_memory(&t, &c, 0, 0) == TX_OK);
        assert(step(0)); assert(!step(60000)); assert(!step(240000));
        assert(t.repeat_waiting);
        if (action == 0) assert(tx_engine_append(&t, "Q", 240001) == TX_OK);
        if (action == 1) assert(tx_engine_memory(&t, &c, 1, 240001) == TX_OK);
        if (action == 2) tx_engine_cancel(&t);
        if (action == 3) assert(!tx_engine_step(&t, &c, 240001, true));
        assert(!t.repeat && !t.repeat_waiting);
    }
    reset(); c.wpm = 5;
    assert(tx_engine_append(&t, "T", 0) == TX_OK);
    assert(step(0)); assert(step(719999)); assert(!step(720000));
    reset(); assert(tx_engine_append(&t, " E", 0) == TX_OK);
    assert(!step(0)); assert(!step(419999)); assert(step(420000));
}
static void fifo_delay(void)
{
    reset(); c.tx_delay_s = 1;
    assert(tx_engine_append(&t, "e", 0) == TX_OK); assert(!step(999999));
    assert(tx_engine_append(&t, "t", 900000) == TX_OK); assert(!step(1899999));
    assert(step(1900000)); tx_engine_backspace(&t); assert(!t.count);
    tx_engine_backspace(&t); assert(t.phase == TX_ELEMENT && step(1900001));
    tx_engine_cancel(&t); assert(!t.down && !t.count && !t.repeat);
    assert(tx_engine_append(&t, "T", 2000000) == TX_OK); tx_engine_start(&t);
    assert(step(2000000));
    reset(); char full[513]; memset(full, 'e', 511); full[511] = 0;
    assert(tx_engine_append(&t, full, 0) == TX_OK && t.count == 511);
    assert(tx_engine_append(&t, "E", 0) == TX_FULL && t.count == 511);
    assert(step(0) && t.count == 510);
    assert(tx_engine_append(&t, "t", 1) == TX_OK && t.count == 511);
    tx_engine_backspace(&t); assert(t.count == 510);
    tx_engine_cancel(&t); assert(tx_engine_append(&t, "E#T", 0) == TX_UNSUPPORTED && !t.count);
    assert(tx_engine_append(&t, "abcdefghijklmnopqrstuvwxyz0123456789.,?/=!'-()\":;+_@ ", 0) == TX_OK);
}
static void memories(void)
{
    for (unsigned i = 0; i < 5; ++i) {
        reset(); strcpy(c.messages[i], "E");
        assert(tx_engine_memory(&t, &c, i, 0) == TX_OK);
        assert(step(0)); assert(!step(60000)); assert(!step(240000));
        assert(t.repeat == (i == 0));
        assert(!step(10059999)); assert(step(10060000) == (i == 0));
    }
    reset(); strcpy(c.messages[0], "E");
    assert(tx_engine_memory(&t, &c, 0, 0) == TX_OK); assert(step(0));
    assert(!tx_engine_step(&t, &c, 1, true));
    assert(!t.count && !t.down && !t.repeat && !t.tune);
    assert(!step(20000000));
    assert(tx_engine_memory(&t, &c, 0, 0) == TX_OK);
    assert(tx_engine_append(&t, "Q", 0) == TX_OK && !t.repeat);
    assert(tx_engine_memory(&t, &c, 0, 0) == TX_OK);
    assert(tx_engine_memory(&t, &c, 2, 0) == TX_OK && !t.repeat);
    assert(tx_engine_memory(&t, &c, 0, 0) == TX_OK);
    tx_engine_cancel(&t); assert(!t.repeat);
    char full[512]; memset(full, 'E', 511); full[511] = 0;
    assert(tx_engine_append(&t, full, 0) == TX_OK);
    strcpy(c.messages[1], "T");
    assert(tx_engine_memory(&t, &c, 1, 0) == TX_FULL && t.count == 511);
    assert(!strcmp(t.fifo, full));
    reset(); c.tx_delay_s = 2; assert(tx_engine_memory(&t, &c, 0, 0) == TX_OK);
    assert(!step(1999999)); assert(step(2000000));
}
static void tune(void)
{
    reset(); tx_engine_tune(&t, 0); assert(step(0)); assert(step(9999999));
    assert(!step(10000000) && !t.tune);
    c.tune_timeout_s = 0; tx_engine_tune(&t, 0); assert(step(90000000));
    tx_engine_tune(&t, 90000001); assert(!step(90000001));
    tx_engine_tune(&t, 0); assert(step(0));
    assert(!tx_engine_step(&t, &c, 1, true) && !t.tune);
    tx_engine_tune(&t, 0); tx_engine_cancel(&t); assert(!step(1));
}
int main(void) { timing(); pending_repeat_cancel(); fifo_delay(); memories(); tune(); puts("keyer K6 TX: PASS"); }

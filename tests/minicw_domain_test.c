#include "keyer_service.h"
#include "keyer_decoder.h"
#include "audio_service.h"
#include "minicw_port.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint64_t now_ms;
static uint32_t tip = 1, ring = 1, out_tip, out_ring;
uint32_t minicw_port_now_ms(void) { return now_ms; }
uint32_t minicw_port_ticks(void) { return now_ms / 10; }
bool minicw_port_inputs_ready(void) { return true; }
bool minicw_port_outputs_ready(void) { return true; }
uint32_t minicw_port_read(uint32_t line) { assert(line == 13 || line == 15); return line == 13 ? tip : ring; }
void minicw_port_write(uint32_t line, uint32_t level)
{
    assert(line == 3 || line == 6);
    if (line == 3) out_tip = level; else out_ring = level;
}
static void init(void)
{
    now_ms = 1000; tip = ring = 1;
    audio_service_init(); keyer_service_init();
    assert(keyer_service_get_key_in_wpm() == 19);
    assert(strcmp(keyer_service_get_message(0), "CQ POTA") == 0);
    assert(out_tip == 1 && out_ring == 1);
}
static void tick(uint64_t ms) { now_ms = ms; keyer_service_update(); }
static keyer_event_t event(keyer_event_type_t type)
{
    keyer_event_t e = keyer_service_poll_event(); assert(e.type == type); return e;
}
static keyer_decoder_result_t decode(const char *pattern)
{
    keyer_decoder_t decoder; keyer_decoder_reset(&decoder);
    for (; *pattern; ++pattern) keyer_decoder_append(&decoder, *pattern == '-');
    keyer_decoder_result_t r = keyer_decoder_finalize(&decoder);
    assert(!keyer_decoder_has_pending(&decoder));
    assert(keyer_decoder_finalize(&decoder).type == KEYER_DECODER_RESULT_NONE);
    return r;
}
static void decoder_vectors(void)
{
    const char *characters = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.,?/=";
    for (const char *p = characters; *p; ++p) {
        keyer_decoder_result_t r = decode(audio_service_get_cw_pattern(*p));
        assert(r.type == KEYER_DECODER_RESULT_CHAR && r.ch == *p);
    }
    for (unsigned i = 6; i <= 12; ++i) {
        char dits[14]; memset(dits, '.', i); dits[i] = 0;
        assert(decode(dits).type == KEYER_DECODER_RESULT_BACKSPACE);
    }
    assert(decode(".-..-.").type == KEYER_DECODER_RESULT_ENTER);
    assert(decode("----").type == KEYER_DECODER_RESULT_SPACE);
    assert(decode(".............").type == KEYER_DECODER_RESULT_INVALID);
    assert(decode(".-.-.").type == KEYER_DECODER_RESULT_INVALID);
}
static void iambic(void)
{
    for (int mode = KEYER_PADDLE_IAMBIC_A; mode <= KEYER_PADDLE_IAMBIC_B; ++mode) {
        init(); keyer_service_set_paddle_mode((keyer_paddle_mode_t)mode);
        tip = ring = 0; tick(1000);
        assert(event(KEYER_EVENT_DIT).duration_ms == 63);
        tip = ring = 1; tick(1010);
        tick(1050); assert(out_tip == 0 && out_ring == 0);
        tick(1060); assert(out_tip == 1 && out_ring == 1);
        tick(1120);
        /* Golden source intentionally applies squeeze-release extra to A, not B. */
        if (mode == KEYER_PADDLE_IAMBIC_A) {
            assert(event(KEYER_EVENT_DAH).duration_ms == 189);
            tick(1300); assert(out_tip == 1 && out_ring == 1);
            tick(1480); assert(event(KEYER_EVENT_CHAR_COMPLETE).decoded_char == 'A');
        } else {
            event(KEYER_EVENT_NONE);
            tick(1240); assert(event(KEYER_EVENT_CHAR_COMPLETE).decoded_char == 'E');
        }
    }
}
static void bug_and_straight(void)
{
    init(); keyer_service_set_paddle_mode(KEYER_PADDLE_BUG);
    ring = 0; tick(1000); event(KEYER_EVENT_DAH);
    tick(1500); assert(out_tip == 0 && out_ring == 0);
    ring = 1; tick(1510); assert(out_tip == 1 && out_ring == 1);
    tick(1690); assert(event(KEYER_EVENT_CHAR_COMPLETE).decoded_char == 'T');
    for (int mode = KEYER_KEY_IN_SK_T; mode <= KEYER_KEY_IN_SK_R; ++mode) {
        init(); keyer_service_set_key_in_mode((keyer_key_in_mode_t)mode);
        if (mode == KEYER_KEY_IN_SK_T) tip = 0; else ring = 0;
        tick(1000); assert(out_tip == 0 && out_ring == 0);
        tip = ring = 1; tick(1060); assert(event(KEYER_EVENT_DIT).duration_ms == 60);
        tick(1240); assert(event(KEYER_EVENT_CHAR_COMPLETE).decoded_char == 'E');
        assert(out_tip == 1 && out_ring == 1);
    }
}
static void outputs(void)
{
    for (int mode = 0; mode <= KEYER_KEY_OUT_OFF; ++mode) {
        for (int dah = 0; dah < 2; ++dah) {
            init(); keyer_service_set_key_out_mode((keyer_key_out_mode_t)mode);
            assert(out_tip == 1 && out_ring == (mode == KEYER_KEY_OUT_SK_M ? 0U : 1U));
            if (dah) ring = 0; else tip = 0;
            tick(1000); event(dah ? KEYER_EVENT_DAH : KEYER_EVENT_DIT);
            if (mode == KEYER_KEY_OUT_PADDLE) assert(out_tip == (unsigned)dah && out_ring == (unsigned)!dah);
            else if (mode == KEYER_KEY_OUT_PADDLE_R) assert(out_tip == (unsigned)!dah && out_ring == (unsigned)dah);
            else if (mode == KEYER_KEY_OUT_OFF) assert(out_tip == 1 && out_ring == 1);
            else assert(out_tip == 0 && out_ring == 0);
            tip = ring = 1; tick(dah ? 1180 : 1060);
            assert(out_tip == 1 && out_ring == (mode == KEYER_KEY_OUT_SK_M ? 0U : 1U));
            keyer_service_set_key_out_mode(KEYER_KEY_OUT_OFF);
            assert(out_tip == 1 && out_ring == 1);
        }
    }
    init(); keyer_service_set_key_in_mode(KEYER_KEY_IN_PADDLE_R);
    ring = 0; tick(1000); event(KEYER_EVENT_DIT);
}
static void preemption(void)
{
    for (int mode = 0; mode <= KEYER_KEY_IN_SK_R; ++mode) {
        init(); keyer_service_set_key_in_mode((keyer_key_in_mode_t)mode);
        assert(keyer_service_tx_append_text("CQ POTA", false));
        keyer_service_tx_start(); tick(1000); assert(keyer_service_is_tx_active());
        if (mode == KEYER_KEY_IN_SK_R) ring = 0; else tip = 0;
        tick(1010); event(KEYER_EVENT_TX_CANCELLED);
        assert(!keyer_service_is_tx_active() && !keyer_service_tx_has_text());
        assert(out_tip == 1 && out_ring == 1);
        tick(2000); event(KEYER_EVENT_NONE); /* cancel press is consumed until release */
        tip = ring = 1; tick(2010);
        if (mode == KEYER_KEY_IN_SK_R) ring = 0; else tip = 0;
        tick(2020); assert(out_tip == 0 && out_ring == 0);
    }
}
static void automatic_and_tune(void)
{
    init(); assert(keyer_service_tx_append_text("ET", false));
    keyer_service_tx_start(); tick(1000); assert(out_tip == 0);
    /* Active character is protected from backspace; only unsent T is removed. */
    assert(keyer_service_tx_backspace()); assert(!keyer_service_tx_backspace());
    tick(1060); assert(out_tip == 1);
    tick(1240); assert(!keyer_service_is_tx_active());
    keyer_service_set_tune_active(true); keyer_service_set_tune_latched(true);
    tick(1250); assert(out_tip == 0 && keyer_service_get_tune_output_active());
    tip = 0; tick(1260); assert(!keyer_service_get_tune_latched() && out_tip == 1);
    tick(1300); assert(out_tip == 1);
    tip = 1; tick(1310); tip = 0; tick(1320); assert(out_tip == 0);
    keyer_service_set_tune_active(false); assert(out_tip == 1 && out_ring == 1);
    init(); /* repeated init resets configuration, FIFO, decoder and tune */
    assert(!keyer_service_get_tune_active());
}
static void bounds_and_wrap(void)
{
    init();
    char text[513]; memset(text, 'E', sizeof(text) - 1); text[512] = 0;
    assert(!keyer_service_tx_append_text(text, false));
    assert(!keyer_service_tx_has_text());
    text[511] = 0; assert(keyer_service_tx_append_text(text, false));
    assert(!keyer_service_tx_append_text("T", false));
    keyer_service_tx_clear();
    uint64_t start = ((uint64_t)UINT32_MAX - 3U) * 10U;
    tip = 0; tick(start); event(KEYER_EVENT_DIT);
    tip = 1; tick(start + 50); assert(out_tip == 0);
    tick(start + 60); assert(out_tip == 1);
    tick(start + 240); assert(event(KEYER_EVENT_CHAR_COMPLETE).decoded_char == 'E');
}
int main(void)
{
    decoder_vectors(); iambic(); bug_and_straight(); outputs(); preemption(); automatic_and_tune(); bounds_and_wrap();
    puts("minicw domain: PASS (pinned decoder/timing/A-B/Bug/KeyOut/cancel/Tune)");
    return 0;
}

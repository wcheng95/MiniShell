#include <assert.h>
#include <stdio.h>
#include <string.h>
/* Inspect the reference's private envelope/cursor checkpoints, not a second renderer. */
#include "../platform/common/tone_stream.c"
static bool take(uint32_t timeout) { (void)timeout; return true; }
static void give(void) { }
static void init(void)
{
    tone_stream_port_t port = {take, give, give, give};
    tone_stream_init(&port, 700);
}
static void zero(const int16_t *pcm) { for (unsigned i = 0; i < 240; ++i) assert(pcm[i] == 0); }
int main(void)
{
    int16_t pcm[240]; tone_stream_commit_t commit;
    init();
    assert(AUDIO_SEG_RING_CAP == 64 && TONE_STREAM_FRAMES == 240 && TONE_STREAM_PRIME_CHUNKS == 8);
    assert(audio_cw_rcos_rise_q15(0, 240) == 0);
    assert(audio_cw_rcos_rise_q15(60, 240) == 4798);
    assert(audio_cw_rcos_rise_q15(120, 240) == 16383);
    assert(audio_cw_rcos_rise_q15(180, 240) == 27967);
    assert(audio_cw_envelope_gain_q15(959, 960, 240, 240) == 0);
    assert(tone_stream_enqueue(20) == MINI_OK && tone_stream_busy());
    uint64_t inc = audio_cw_phase_inc();
    for (unsigned i = 0; i < 4; ++i) {
        tone_stream_render(pcm, &commit);
        assert(commit.samples == 240 && tone_stream_busy());
        tone_stream_committed(&commit);
    }
    assert(!tone_stream_busy() && pcm[239] == 0 && s_phase == inc * 960);
    uint64_t phase = s_phase;
    for (unsigned i = 0; i < 100; ++i) { tone_stream_render(pcm, &commit); zero(pcm); }
    assert(s_phase == phase); /* Golden phase is retained, not advanced on zero gaps. */
    assert(tone_stream_enqueue(3) == MINI_OK);
    tone_stream_render(pcm, &commit); assert(commit.samples == 144 && pcm[143] == 0);
    assert(s_cur_attack_n == 72 && s_cur_release_n == 72);
    tone_stream_committed(&commit); assert(!tone_stream_busy());
    for (unsigned i = 144; i < 240; ++i) assert(!pcm[i]);

    init(); assert(tone_stream_hold(true) == MINI_OK);
    for (unsigned i = 0; i < 8; ++i) { tone_stream_render(pcm, &commit); assert(tone_stream_busy()); }
    assert(s_cur_pos == 240); /* Hold freezes its gain position, not oscillator phase. */
    assert(tone_stream_hold(false) == MINI_OK && !tone_stream_busy());
    tone_stream_render(pcm, &commit); assert(!pcm[239]);
    tone_stream_render(pcm, &commit); zero(pcm);

    for (unsigned pos = 60; pos <= 900; pos += 840) {
        init(); assert(tone_stream_enqueue(20) == MINI_OK);
        assert(audio_cw_refill_cursor()); s_cur_pos = pos;
        uint16_t gain = audio_cw_cursor_gain();
        assert(tone_stream_stop() == MINI_OK);
        tone_stream_render(pcm, &commit);
        assert(s_rel_g0 == gain && pcm[239] == 0 && commit.samples == 0);
        tone_stream_render(pcm, &commit); zero(pcm);
    }
    init(); assert(tone_stream_enqueue(20) == MINI_OK);
    tone_stream_render(pcm, &commit); tone_stream_commit_t old = commit;
    assert(tone_stream_stop() == MINI_OK);
    assert(tone_stream_enqueue(5) == MINI_OK);
    tone_stream_committed(&old); assert(s_keyed_outstanding == 240);
    tone_stream_render(pcm, &commit); assert(commit.samples == 0); /* release tail */
    tone_stream_committed(&commit); assert(tone_stream_busy());
    tone_stream_render(pcm, &commit); assert(commit.samples == 240);
    tone_stream_committed(&commit); assert(!tone_stream_busy());
    init();
    for (unsigned i = 0; i < 64; ++i) assert(tone_stream_enqueue(5) == MINI_OK);
    assert(s_seg_count == 64 && s_keyed_outstanding == 15360);
    assert(tone_stream_enqueue(5) == MINI_ERR_NO_SPACE);
    for (unsigned i = 0; i < 64; ++i) { tone_stream_render(pcm, &commit); tone_stream_committed(&commit); }
    assert(!tone_stream_busy());
    assert(tone_stream_enqueue(0) == MINI_ERR_INVALID);
    assert(tone_stream_enqueue(60001) == MINI_ERR_INVALID);
    puts("tone stream: exact reference envelope/phase/ring/preemption/generation PASS");
}

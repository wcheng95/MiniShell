#include "tone_sim.h"
#include "tone_stream.h"
static uint64_t (*s_clock)(void *);
static void *s_ctx;
static bool s_open;
static uint64_t s_due;
static bool lock(uint32_t ms) { (void)ms; return true; }
static void unlock(void) { }
static void pump(void)
{
    uint64_t now = s_clock(s_ctx);
    int16_t pcm[TONE_STREAM_FRAMES];
    while (now >= s_due) {
        tone_stream_commit_t commit;
        tone_stream_render(pcm, &commit);
        tone_stream_committed(&commit);
        s_due += 5000;
    }
}
static mini_result_t open_tone(const mini_audio_tone_config_t *config, mini_audio_tone_t *out)
{
    if (s_open) return MINI_ERR_TOO_MANY_OPEN;
    tone_stream_port_t port = {lock, unlock, unlock, unlock};
    tone_stream_init(&port, (uint16_t)config->pitch_hz);
    s_due = s_clock(s_ctx) + 5000;
    s_open = true; *out = 1;
    return MINI_OK;
}
static mini_result_t valid(mini_audio_tone_t h)
{
    if (!s_open || h != 1) return MINI_ERR_BAD_HANDLE;
    pump(); return MINI_OK;
}
static mini_result_t configure(mini_audio_tone_t h, const mini_audio_tone_config_t *c)
{ mini_result_t r = valid(h); return r == MINI_OK ? tone_stream_pitch((uint16_t)c->pitch_hz) : r; }
static mini_result_t enqueue(mini_audio_tone_t h, uint32_t ms)
{ mini_result_t r = valid(h); return r == MINI_OK ? tone_stream_enqueue(ms) : r; }
static mini_result_t hold(mini_audio_tone_t h, uint32_t active)
{ mini_result_t r = valid(h); return r == MINI_OK ? tone_stream_hold(active != 0) : r; }
static mini_result_t stop(mini_audio_tone_t h)
{ mini_result_t r = valid(h); return r == MINI_OK ? tone_stream_stop() : r; }
static mini_result_t busy(mini_audio_tone_t h, uint32_t *out)
{ mini_result_t r = valid(h); if (r == MINI_OK) *out = tone_stream_busy(); return r; }
static mini_result_t close_tone(mini_audio_tone_t h)
{
    mini_result_t r = valid(h);
    if (r == MINI_OK) { (void)tone_stream_stop(); s_open = false; }
    return r;
}
static const mini_audio_tone_api_t api = {sizeof(api), open_tone, configure, enqueue, hold, stop, busy, close_tone};
void tone_sim_configure(minishell_services_port_t *port)
{
    s_clock = port->monotonic_us; s_ctx = port->ctx; s_open = false;
    if (s_clock) { port->audio_tone = &api; port->audio_capabilities |= MINI_AUDIO_CAP_TONE; }
}

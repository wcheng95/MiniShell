#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "minishell_services.h"
#include "tone_sim.h"
static uint64_t now;
static uint64_t clock_us(void *ctx) { (void)ctx; return now; }
static mini_result_t pcm_open(void *ctx, const char *endpoint, uint32_t hz, uint32_t format, uint32_t channels, minishell_backend_audio_t *out)
{ (void)ctx; (void)endpoint; (void)hz; (void)format; (void)channels; *out = 55; return MINI_OK; }
static mini_result_t pcm_action(void *ctx, minishell_backend_audio_t h) { (void)ctx; (void)h; return MINI_OK; }
static mini_result_t pcm_write(void *ctx, minishell_backend_audio_t h, const void *data, uint32_t frames, uint32_t *out, uint32_t timeout)
{ (void)ctx; (void)h; (void)data; (void)timeout; *out = frames; return MINI_OK; }
int main(void)
{
    minishell_services_port_t port = {0};
    port.monotonic_us = clock_us;
    port.audio_capabilities = MINI_AUDIO_CAP_TX;
    port.audio_tx_open = pcm_open; port.audio_tx_start = port.audio_tx_stop = port.audio_tx_abort = port.audio_tx_close = pcm_action;
    port.audio_tx_write = pcm_write;
    minishell_services_configure(&port);
    const mini_audio_api_t *audio = mini_api_get()->audio;
    assert(audio && audio->tx && !audio->tone && audio->capabilities == MINI_AUDIO_CAP_TX);
    /* Old provider remains PCM-only; the optional table is not presumed present. */
    tone_sim_configure(&port);
    mini_audio_tone_api_t truncated = *port.audio_tone;
    const mini_audio_tone_api_t *provider = port.audio_tone;
    truncated.struct_size = offsetof(mini_audio_tone_api_t, close);
    port.audio_tone = &truncated;
    minishell_services_configure(&port); assert(!mini_api_get()->audio->tone);
    port.audio_tone = provider;
    minishell_services_configure(&port); minishell_services_app_begin();
    audio = mini_api_get()->audio;
    assert(audio->struct_size >= offsetof(mini_audio_api_t, tone) + sizeof(audio->tone));
    assert(audio->capabilities == (MINI_AUDIO_CAP_TX | MINI_AUDIO_CAP_TONE));
    const mini_audio_tone_api_t *tone = audio->tone;
    mini_audio_tone_config_t c = {sizeof(c), 700, 80}; mini_audio_tone_t h, other;
    assert(tone->open(NULL, &h) == MINI_ERR_INVALID);
    c.pitch_hz = 299; assert(tone->open(&c, &h) == MINI_ERR_INVALID);
    c.pitch_hz = 1000; assert(tone->open(&c, &h) == MINI_ERR_INVALID);
    c.pitch_hz = 700; c.volume = 100; assert(tone->open(&c, &h) == MINI_ERR_INVALID);
    c.volume = 80;
    assert(tone->open(&c, &h) == MINI_OK && h);
    assert(tone->open(&c, &other) == MINI_ERR_TOO_MANY_OPEN);
    assert(tone->configure(h + 1, &c) == MINI_ERR_BAD_HANDLE);
    assert(tone->busy(h, NULL) == MINI_ERR_INVALID);
    assert(tone->enqueue(h, 0) == MINI_ERR_INVALID);
    assert(tone->hold(h, 2) == MINI_ERR_INVALID);
    assert(tone->enqueue(h, 63) == MINI_OK);
    uint32_t busy = 0;
    assert(tone->busy(h, &busy) == MINI_OK && busy);
    now = 60000; assert(tone->busy(h, &busy) == MINI_OK && busy);
    now = 65000; assert(tone->busy(h, &busy) == MINI_OK && !busy);
    assert(tone->hold(h, 1) == MINI_OK); now += 100000;
    assert(tone->busy(h, &busy) == MINI_OK && busy);
    assert(tone->stop(h) == MINI_OK);
    assert(tone->busy(h, &busy) == MINI_OK && !busy);
    mini_audio_format_t f = {sizeof(f), 48000, MINI_AUDIO_SAMPLE_S16, 1}; mini_audio_stream_t pcm;
    assert(audio->tx->open("speaker", &f, &pcm) == MINI_ERR_TOO_MANY_OPEN);
    assert(tone->close(h) == MINI_OK);
    assert(tone->enqueue(h, 10) == MINI_ERR_BAD_HANDLE);
    assert(audio->tx->open("speaker", &f, &pcm) == MINI_OK);
    assert(tone->open(&c, &other) == MINI_ERR_TOO_MANY_OPEN);
    assert(audio->tx->close(pcm) == MINI_OK);
    assert(tone->open(&c, &other) == MINI_OK && other != h);
    assert(tone->hold(other, 1) == MINI_OK);
    minishell_services_app_end();
    assert(tone->busy(other, &busy) == MINI_ERR_BAD_HANDLE);
    minishell_services_app_begin(); assert(tone->open(&c, &h) == MINI_OK);
    minishell_services_app_end();
    puts("tone API: optional capability/validation/busy/PCM exclusion/app cleanup PASS");
}

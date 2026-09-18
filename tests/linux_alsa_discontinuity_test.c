#include <assert.h>
#include <stdio.h>
/* Private provider fault injection; no hardware or production test endpoint. */
#include "../platform/linux/linux_audio_wav.c"

#ifdef MINISHELL_LINUX_HAVE_ALSA
static int wait_result, recovery_result, reads, recoveries;
static int fake_wait(snd_pcm_t *pcm, int timeout)
{ (void)pcm; (void)timeout; return wait_result; }
static int fake_recover(snd_pcm_t *pcm, int error, int silent)
{ (void)pcm; assert(error < 0 && silent == 1); ++recoveries; return recovery_result; }
static snd_pcm_sframes_t fake_read(snd_pcm_t *pcm, void *buffer, snd_pcm_uframes_t size)
{
    (void)pcm;
    if (++reads > 1) return -EPIPE;
    memset(buffer, 1, size * ALSA_NATIVE_FRAME_BYTES);
    return (snd_pcm_sframes_t)size;
}
#endif
int main(void)
{
#ifdef MINISHELL_LINUX_HAVE_ALSA
    s_alsa.pcm = (snd_pcm_t *)(uintptr_t)1;
    s_alsa.started = true;
    s_alsa.pcm_wait = fake_wait;
    s_alsa.pcm_readi = fake_read;
    s_alsa.pcm_recover = fake_recover;
    int16_t frames[512];
    uint32_t got;
    wait_result = -EPIPE;
    s_alsa.decimation_phase = 3;
    assert(audio_rx_read(NULL, ALSA_AUDIO_HANDLE, frames, 256, &got, 1) == MINI_ERR_DISCONTINUITY);
    assert(got == 0 && s_alsa.decimation_phase == 0 && s_alsa.started);
    wait_result = 1;
    s_alsa.decimation_phase = 2;
    assert(audio_rx_read(NULL, ALSA_AUDIO_HANDLE, frames, 256, &got, 1) == MINI_ERR_DISCONTINUITY);
    assert(reads == 2 && got == 0 && s_alsa.decimation_phase == 0);
    recovery_result = -1;
    assert(audio_rx_read(NULL, ALSA_AUDIO_HANDLE, frames, 256, &got, 1) == MINI_ERR_IO);
    assert(got == 0 && recoveries == 3);
    wait_result = -EPIPE;
    assert(audio_rx_read(NULL, ALSA_AUDIO_HANDLE, frames, 256, &got, 1) == MINI_ERR_IO);
    recovery_result = 0;
    wait_result = 1;
    reads = 0;
    assert(audio_rx_read(NULL, ALSA_AUDIO_HANDLE, frames, 1, &got, 1) == MINI_OK);
    assert(got == 1 && s_alsa.started);
    puts("ALSA recovery mapping: PASS");
#else
    puts("ALSA development headers unavailable");
    return 77;
#endif
    return 0;
}

#include <assert.h>
#include <stdio.h>
/* Provider-private injection: exercise endpoint/native-mode selection without a server. */
#include "../platform/linux/linux_audio_wav.c"

#ifdef MINISHELL_LINUX_HAVE_ALSA
static char opened[272];
static int open_error, params_error, wait_result=1, read_error, recover_error;
static unsigned closes, reads, recoveries, mode_channels, native_index, format_checks;
static bool pulse_mode;
static unsigned requested_rate=12000, requested_channels=2;
static int fake_open(snd_pcm_t **pcm, const char *name, snd_pcm_stream_t stream, int mode)
{
    assert(stream == SND_PCM_STREAM_CAPTURE);
    pulse_mode = strncmp(name,"pulse:DEVICE=",13)==0;
    assert(mode == (pulse_mode ? SND_PCM_NONBLOCK : 0));
    strcpy(opened,name);
    if(open_error) return -1;
    *pcm=(snd_pcm_t *)(uintptr_t)1; return 0;
}
static int fake_close(snd_pcm_t *pcm) { assert(pcm); ++closes; return 0; }
static int fake_params(snd_pcm_t *pcm, snd_pcm_format_t format, snd_pcm_access_t access,
                       unsigned channels, unsigned rate, int soft, unsigned latency)
{
    assert(pcm && access==SND_PCM_ACCESS_RW_INTERLEAVED && soft==0 && latency==10000);
    assert(format==(pulse_mode?SND_PCM_FORMAT_S16_LE:SND_PCM_FORMAT_S24_3LE));
    assert(rate==(pulse_mode?requested_rate:48000) && channels==(pulse_mode?requested_channels:2));
    mode_channels=channels; ++format_checks;
    return params_error;
}
static int fake_ok(snd_pcm_t *pcm) { assert(pcm); return 0; }
static int fake_wait(snd_pcm_t *pcm, int timeout) { assert(pcm && timeout==20); return wait_result; }
static int fake_recover(snd_pcm_t *pcm, int error, int silent)
{ assert(pcm && error<0 && silent); ++recoveries; return recover_error; }
static snd_pcm_sframes_t fake_read(snd_pcm_t *pcm, void *buffer, snd_pcm_uframes_t n)
{
    assert(pcm && n<=256); ++reads;
    if(read_error) return read_error;
    /* Odd short native chunks verify phase retention for QMX. */
    if(n>5) n=5;
    uint8_t *p=buffer;
    for(unsigned i=0;i<n;++i,++native_index) for(unsigned c=0;c<mode_channels;++c) {
        uint16_t v=(uint16_t)(c ? -(int)(native_index+1) : (int)(native_index+1));
        if(!pulse_mode) *p++=0;
        *p++=(uint8_t)v; *p++=(uint8_t)(v>>8);
    }
    return (snd_pcm_sframes_t)n;
}
static void setup(void)
{
    memset(&s_alsa,0,sizeof(s_alsa));
    s_alsa.library=(void *)(uintptr_t)1;
    s_alsa.pcm_open=fake_open; s_alsa.pcm_close=fake_close;
    s_alsa.pcm_set_params=fake_params; s_alsa.pcm_prepare=fake_ok;
    s_alsa.pcm_start=fake_ok; s_alsa.pcm_drop=fake_ok;
    s_alsa.pcm_wait=fake_wait; s_alsa.pcm_readi=fake_read; s_alsa.pcm_recover=fake_recover;
}
#endif
int main(void)
{
#ifdef MINISHELL_LINUX_HAVE_ALSA
    setup();
    char name[272];
    const char *bad[]={"pulse:","pulse:x,DEVICE=y","pulse:x'","pulse:x\n","pulse:x y","pulse:x\"","pulse:x;test"};
    minishell_backend_audio_t handle=0;
    for(size_t i=0;i<sizeof(bad)/sizeof(bad[0]);++i) {
        assert(!pulse_pcm_name(bad[i],name));
        assert(audio_rx_open(NULL,bad[i],12000,MINI_AUDIO_SAMPLE_S16,2,&handle)==MINI_ERR_INVALID);
        assert(!handle);
    }
    char long_name[264]; memset(long_name,'a',sizeof(long_name)); memcpy(long_name,"pulse:",6); long_name[263]=0;
    assert(!pulse_pcm_name(long_name,name));
    assert(audio_rx_open(NULL,"pulse:source",0,MINI_AUDIO_SAMPLE_S16,2,&handle)==MINI_ERR_UNSUPPORTED);
    assert(audio_rx_open(NULL,"pulse:source",12000,MINI_AUDIO_SAMPLE_S16,3,&handle)==MINI_ERR_UNSUPPORTED);
    const char *sources[]={"pulse:@DEFAULT_MONITOR@","pulse:alsa_output.pci-0000_00_1f.3.analog-stereo.monitor"};
    for(unsigned i=0;i<2;++i) {
        assert(!audio_rx_open(NULL,sources[i],12000,MINI_AUDIO_SAMPLE_S16,2,&handle));
        assert(!strcmp(opened+13,sources[i]+6) && s_alsa.pulse);
        minishell_backend_audio_t other=0;
        assert(audio_rx_open(NULL,"alsa:hw:2,0",12000,MINI_AUDIO_SAMPLE_S16,2,&other)==MINI_ERR_TOO_MANY_OPEN);
        int16_t samples[512]; uint32_t got=99;
        assert(audio_rx_read(NULL,handle,samples,256,&got,20)==MINI_ERR_NOT_READY && got==0);
        assert(!audio_rx_start(NULL,handle));
        native_index=0;
        assert(!audio_rx_read(NULL,handle,samples,256,&got,20) && got==5);
        for(unsigned j=0;j<5;++j) assert(samples[j*2]==(int)j+1 && samples[j*2+1]==-(int)j-1);
        wait_result=0;
        unsigned before=reads;
        assert(!audio_rx_read(NULL,handle,samples,256,&got,20) && !got && reads==before);
        wait_result=-EPIPE;
        assert(audio_rx_read(NULL,handle,samples,256,&got,20)==MINI_ERR_DISCONTINUITY && !got);
        wait_result=1; read_error=-EAGAIN;
        assert(!audio_rx_read(NULL,handle,samples,256,&got,20) && !got);
        read_error=-EPIPE;
        assert(audio_rx_read(NULL,handle,samples,256,&got,20)==MINI_ERR_DISCONTINUITY && !got);
        recover_error=-1;
        assert(audio_rx_read(NULL,handle,samples,256,&got,20)==MINI_ERR_IO && !got);
        recover_error=read_error=0;
        assert(!audio_rx_stop(NULL,handle) && !audio_rx_close(NULL,handle));
        assert(!s_alsa.pulse && !s_alsa.pcm);
    }
    /* Existing QMX native format, no soft resampling, /4 phase and 10 ms latency. */
    assert(!audio_rx_open(NULL,"alsa:hw:2,0",12000,MINI_AUDIO_SAMPLE_S16,2,&handle));
    assert(!strcmp(opened,"hw:2,0") && !s_alsa.pulse);
    assert(!audio_rx_start(NULL,handle)); native_index=0;
    int16_t samples[512]; uint32_t got;
    assert(!audio_rx_read(NULL,handle,samples,256,&got,20) && got==2);
    assert(samples[0]==1 && samples[2]==5 && s_alsa.decimation_phase==1);
    assert(!audio_rx_read(NULL,handle,samples,256,&got,20) && got==1);
    assert(samples[0]==9 && s_alsa.decimation_phase==2);
    assert(!audio_rx_stop(NULL,handle) && !audio_rx_close(NULL,handle));
    requested_rate=16000; requested_channels=1;
    assert(!audio_rx_open(NULL,sources[0],16000,MINI_AUDIO_SAMPLE_S16,1,&handle));
    assert(!audio_rx_start(NULL,handle)); native_index=0;
    assert(!audio_rx_read(NULL,handle,samples,256,&got,20) && got==5);
    for(unsigned i=0;i<5;++i) assert(samples[i]==(int)i+1);
    assert(!audio_rx_stop(NULL,handle) && !audio_rx_close(NULL,handle));
    requested_rate=12000; requested_channels=2;
    open_error=1;
    assert(audio_rx_open(NULL,sources[0],12000,MINI_AUDIO_SAMPLE_S16,2,&handle)==MINI_ERR_IO && !handle);
    open_error=0; params_error=-1;
    assert(audio_rx_open(NULL,sources[0],12000,MINI_AUDIO_SAMPLE_S16,2,&handle)==MINI_ERR_UNSUPPORTED && !handle);
    assert(closes==5 && format_checks==5 && recoveries==6);
    puts("Pulse/QMX endpoint formats, PCM, lifecycle and recovery: PASS");
#else
    puts("ALSA development headers unavailable"); return 77;
#endif
    return 0;
}

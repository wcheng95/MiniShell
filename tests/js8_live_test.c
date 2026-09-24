#include "js8_live.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned allocations, audio_handles, serial_handles, file_handles, starts, stops, syncs, utc_queries;
static unsigned reads, total_frames, quit_after, failure, worker_delay;
static Js8Live *working;
static char cat[128], log_bytes[65536];
static size_t cat_size, log_size;
static uint64_t clock_us;
static mini_result_t mem_alloc(uint32_t n, void **p) { *p = malloc(n); if (*p) ++allocations; return *p ? 0 : MINI_ERR_NO_MEMORY; }
static mini_result_t mem_free(void *p) { if (p) { --allocations; free(p); } return 0; }
static void quiet(const char *s) { (void)s; }
static uint64_t mono(void) { return ++clock_us; }
static mini_result_t sleep_ms(uint32_t ms) { (void)ms; if (working) js8_live_decode_step(working); return 0; }
static mini_result_t utc(mini_utc_time_t *t)
{
    ++utc_queries;
    uint64_t samples = total_frames/2;
    t->unix_seconds = 15000 + (int64_t)(samples/6000);
    t->nanoseconds = (uint32_t)((samples%6000)*1000000000/6000 + 1);
    return 0;
}
static mini_result_t audio_open(const char *path, const mini_audio_format_t *f, mini_audio_stream_t *h)
{
    assert(!strcmp(path, "fake"));
    assert(f->sample_rate_hz == 12000 && f->sample_format == MINI_AUDIO_SAMPLE_S16 && f->channels == 2);
    if (failure == 1) return MINI_ERR_IO;
    ++audio_handles; *h = 1; return 0;
}
static mini_result_t audio_start(mini_audio_stream_t h) { assert(h == 1); if (failure == 2) return MINI_ERR_IO; ++starts; return 0; }
static mini_result_t audio_read(mini_audio_stream_t h, void *p, uint32_t cap, uint32_t *got, uint32_t timeout)
{
    assert(h == 1 && cap == 256 && timeout == 20); ++reads;
    if (working && reads > worker_delay) js8_live_decode_step(working);
    if (failure == 3 && reads == 40) { *got = 0; return MINI_ERR_DISCONTINUITY; }
    if (reads > 2200) return MINI_ERR_END_OF_STREAM;
    *got = cap; total_frames += cap; memset(p, 0, cap*4); return 0;
}
static mini_result_t audio_stop(mini_audio_stream_t h) { assert(h == 1); ++stops; return 0; }
static mini_result_t audio_close(mini_audio_stream_t h) { assert(h == 1); --audio_handles; return 0; }
static mini_result_t serial_open(const char *p, mini_serial_t *h) { assert(!strcmp(p,"serial:fake")); ++serial_handles; *h=1; return 0; }
static mini_result_t serial_write(mini_serial_t h, const void *p, uint32_t n, uint32_t *got, uint32_t wait)
{
    assert(h == 1 && wait == 200); if (failure == 4) return MINI_ERR_IO;
    memcpy(cat+cat_size,p,n); cat_size+=n; *got=n; return 0;
}
static mini_result_t serial_close(mini_serial_t h) { assert(h == 1); --serial_handles; return 0; }
static mini_result_t fs_open(const char *p, uint32_t flags, mini_file_t *h)
{
    assert(!strcmp(p,"/flash/log"));
    assert(flags == (MINI_FS_WRITE|MINI_FS_CREATE|MINI_FS_APPEND));
    if (failure == 5) return MINI_ERR_IO;
    ++file_handles; *h=1; return 0;
}
static mini_result_t fs_write(mini_file_t h,const void *p,uint32_t n,uint32_t *written)
{
    assert(h==1 && log_size+n < sizeof(log_bytes));
    if (failure == 6) { *written=0; return MINI_ERR_IO; }
    memcpy(log_bytes+log_size,p,n); log_size+=n; log_bytes[log_size]=0; *written=n; return 0;
}
static mini_result_t fs_sync(mini_file_t h) { assert(h==1); ++syncs; return failure == 7 ? MINI_ERR_IO : 0; }
static mini_result_t fs_close(mini_file_t h) { assert(h==1); --file_handles; return 0; }
static mini_result_t key_read(mini_key_event_t *e,uint32_t timeout)
{
    assert(!timeout);
    if (quit_after && reads >= quit_after) { e->type=MINI_KEY_EVENT_CHAR; e->codepoint='q'; return 0; }
    return MINI_ERR_TIMEOUT;
}
static mini_memory_api_t memory = {.struct_size=sizeof(memory),.alloc=mem_alloc,.free=mem_free};
static mini_time_location_api_t time_api = {.struct_size=sizeof(time_api),.capabilities=MINI_TIMELOC_CAP_UTC,.utc_get=utc,.monotonic_us=mono,.sleep_ms=sleep_ms};
static mini_audio_rx_api_t rx = {sizeof(rx),audio_open,audio_start,audio_read,audio_stop,audio_close};
static mini_audio_api_t audio = {.struct_size=sizeof(audio),.capabilities=MINI_AUDIO_CAP_RX,.rx=&rx};
static mini_serial_api_t serial = {.struct_size=sizeof(serial),.capabilities=MINI_SERIAL_CAP_WRITE,.open=serial_open,.write=serial_write,.close=serial_close};
static mini_fs_api_t fs = {.struct_size=sizeof(fs),.open=fs_open,.write=fs_write,.sync=fs_sync,.close=fs_close};
static mini_key_input_api_t keys = {.struct_size=sizeof(keys),.read=key_read};
static mini_input_api_t input = {.struct_size=sizeof(input),.capabilities=MINI_INPUT_CAP_KEY,.key=&keys};
static mini_system_api_t system_api = {.struct_size=sizeof(system_api),.write=quiet};
static mini_api_t api = {.api_version=MINISHELL_API_VERSION,.struct_size=sizeof(api),.memory=&memory,.time_location=&time_api,.audio=&audio,.serial=&serial,.fs=&fs,.input=&input,.system=&system_api};
static int worker_start(Js8Live *s) { working=s; return 0; }
static void worker_stop(Js8Live *s) {
    assert(s==working);
    if (worker_delay) assert(s->dropped >= 1 && reads > 1500);
    if (failure==3) assert(s->discontinuities==1);
    working=NULL;
}
static const Js8Worker worker = {worker_start,worker_stop};
static unsigned begins, ready, drops, samples_seen;
static float first_sample;
static uint32_t begun_slot;
static int slot_sink(void *ctx, Js8SlotEvent e, uint32_t slot, const float *p, size_t n)
{
    (void)ctx; (void)slot;
    if (e==JS8_SLOT_BEGIN) { ++begins; begun_slot=slot; samples_seen=0; }
    if (e==JS8_SLOT_SAMPLES) { if (!samples_seen) first_sample=*p; samples_seen+=(unsigned)n; }
    if (e==JS8_SLOT_READY) { ++ready; assert(samples_seen==89280); }
    if (e==JS8_SLOT_DROP) ++drops;
    return 0;
}
static void timing_tests(void)
{
    int16_t stereo[1026]; float a[257], b[257];
    for (unsigned i=0;i<1026;++i) stereo[i]=(int16_t)(i*13-1000);
    Js8Frontend s={0}, split={0}; size_t count,n=0;
    assert(!js8_frontend_process(&s,stereo,513,a,257,&count) && count==257);
    for (unsigned i=0;i<513;) {
        unsigned take=(i%11)+1; if (take>513-i) take=513-i;
        size_t got; assert(!js8_frontend_process(&split,stereo+2*i,take,b+n,257-n,&got)); n+=got;i+=take;
    }
    assert(n==count && !memcmp(a,b,n*sizeof(float)) && s.phase==split.phase);
    assert(a[0]==((int32_t)stereo[0]+stereo[1])/65536.0f);
    Js8Frontend old=s; assert(js8_frontend_process(&s,stereo,513,a,0,&count)==-1 && s.phase==old.phase);
    int64_t slot; uint32_t offset;
    assert(!js8_live_anchor(15,0,128,&slot,&offset) && slot==0 && offset==89872);
    assert(!js8_live_anchor(-1,0,128,&slot,&offset) && slot==-1 && offset==83872);
    assert(js8_live_anchor(0,1000000000,0,&slot,&offset)==-1);
    Js8SlotScheduler scheduler={0}; float data[128];
    for(unsigned i=0;i<128;++i)data[i]=(float)i;
    /* Initial tolerance: early arrivals wait for pre; late <=40 ms start S. */
    const int delta[]={-241,-240,-1,0,1,239,240,241};
    for (unsigned i=0;i<sizeof(delta)/sizeof(delta[0]);++i) {
        memset(&scheduler,0,sizeof(scheduler)); begins=ready=drops=0;
        assert(!js8_slot_feed(&scheduler,999,(uint32_t)(80400+delta[i]),data,1,slot_sink,NULL));
        assert(scheduler.next_slot==(delta[i]>=0?1001:1000));
        assert(begins==(unsigned)(delta[i]>=0 && delta[i]<=240));
        if(begins) assert(begun_slot==1000 && first_sample==0);
        assert(!drops);
    }
    memset(&scheduler,0,sizeof(scheduler)); begins=ready=drops=0;
    assert(!js8_slot_feed(&scheduler,999,80350,data,100,slot_sink,NULL));
    assert(begins==1 && begun_slot==1000 && first_sample==50 && samples_seen==50);
    /* Real failure: ~20 ms chunks leave a UTC timing gap over every pre.
     * No discontinuity, no exact hit/cross; all three windows must finalize. */
    memset(&scheduler,0,sizeof(scheduler)); begins=ready=drops=0;
    for (uint32_t target=1000;target<1003;++target) {
        assert(!js8_slot_feed(&scheduler,target-1,80260,data,120,slot_sink,NULL));
        assert(begins==target-1000);
        assert(!js8_slot_feed(&scheduler,target-1,80420,data,120,slot_sink,NULL));
        assert(begins==target-999 && begun_slot==target && first_sample==0);
        for(uint32_t n=120;n<89280;n+=120) {
            uint64_t pos=(uint64_t)(target-1)*90000+80420+n;
            assert(!js8_slot_feed(&scheduler,(uint32_t)(pos/90000),(uint32_t)(pos%90000),data,120,slot_sink,NULL));
        }
        assert(ready==target-999 && samples_seen==89280 && !drops);
    }
    /* Advancing fresh UTC locates pre at index 50, despite sample-count gap. */
    assert(!js8_slot_feed(&scheduler,1002,80350,data,100,slot_sink,NULL));
    assert(begins==4 && begun_slot==1003 && first_sample==50);
    memset(&scheduler,0,sizeof(scheduler)); begins=ready=drops=0;
    for(uint32_t n=0;n<90000;n+=120) {
        uint64_t pos=UINT64_C(1000)*90000-9600+n;
        assert(!js8_slot_feed(&scheduler,(uint32_t)(pos/90000),(uint32_t)(pos%90000),data,120,slot_sink,NULL));
    }
    assert(ready==1 && begins==1);
    /* Consuming 90000 samples cannot advance capture without fresh UTC pre. */
    assert(!js8_slot_feed(&scheduler,1000,80280,data,120,slot_sink,NULL));
    assert(begins==1);
    assert(!js8_slot_feed(&scheduler,1000,80400,data,120,slot_sink,NULL));
    assert(begins==2 && begun_slot==1001 && first_sample==0 && !drops);

}
static void delay_tests(void)
{
    Js8LiveOptions options;
    char *plain[]={"js8chat","--rx","fake"};
    assert(!js8_live_options(3,plain,&options) && options.rx_delay_ms==0);
    char *values[]={"0","1","750","5000","-1","5001","x","1.0","+1","","9999999999999999999999999"};
    for(unsigned i=0;i<sizeof(values)/sizeof(values[0]);++i) {
        char *args[]={"js8chat","--rx","fake","--rx-delay-ms",values[i]};
        assert((js8_live_options(5,args,&options)==0)==(i<4));
    }
    char *duplicate[]={"js8chat","--rx","fake","--rx-delay-ms","0","--rx-delay-ms","1"};
    assert(js8_live_options(7,duplicate,&options));
    assert(js8_live_options(4,duplicate,&options));
    int64_t seconds=15, slot; uint32_t ns=100000000, offset;
    assert(!js8_live_delay(&seconds,&ns,750) && seconds==14 && ns==350000000);
    assert(!js8_live_anchor(seconds,ns,128,&slot,&offset) && slot==0 && offset==85972);
    seconds=16; ns=0;
    assert(!js8_live_delay(&seconds,&ns,1000) && seconds==15 && ns==0);
    assert(!js8_live_anchor(seconds,ns,128,&slot,&offset) && slot==0 && offset==89872);
    seconds=15; ns=750000000;
    assert(!js8_live_delay(&seconds,&ns,750) && seconds==15 && ns==0);
    seconds=0; ns=0;
    assert(!js8_live_delay(&seconds,&ns,1) && seconds==-1 && ns==999000000);
    seconds=INT64_MIN; ns=0;
    assert(js8_live_delay(&seconds,&ns,1) && seconds==INT64_MIN && ns==0);
    seconds=20; ns=123456789;
    assert(!js8_live_delay(&seconds,&ns,0) && seconds==20 && ns==123456789);
    assert(!js8_live_delay(&seconds,&ns,5000) && seconds==15 && ns==123456789);
    assert(js8_live_delay(&seconds,&ns,5001));
}
static Js8DecodedPayload payload(const char *bits, unsigned flags)
{
    Js8DecodedPayload p={0}; p.candidate.freq_offset=128;p.candidate.score=20;
    for(unsigned i=0;i<75;++i)p.payload_bits[i]=(uint8_t)(bits[i]-'0');
    for(unsigned i=0;i<3;++i)p.payload_bits[72+i]=(flags>>(2-i))&1;
    return p;
}
static void semantic_tests(void)
{
    const char *header="011010001101011000101010010110011110111000011000010001110001111100000000010";
    const char *text="100001110011001111001111111010010111111100000110011111011011111111111111001";
    Js8Live *s=malloc(sizeof(*s)); assert(s && !js8_live_init(s,&api));
    s->log_file=1; ++file_handles; log_size=0;
    Js8DecodedPayload h=payload(header,1), d=payload(text,2);
    assert(!js8_live_payload(s,1000,&h)); js8_live_reset(s);
    assert(!s->frontend.phase && !s->scheduler.scheduled && !s->monitor.num_blocks);
    assert(!js8_live_payload(s,1001,&d)); assert(!strstr(log_bytes,"\"MESSAGE\""));
    assert(!js8_live_payload(s,1002,&h)); assert(!js8_live_payload(s,1003,&d));
    assert(strstr(log_bytes,"\"MESSAGE\"") && strstr(log_bytes,"HELLO WORLD"));
    assert(strstr(log_bytes,"1970-01-01T04:10:45Z"));
    failure=6; assert(js8_live_payload(s,1004,&h)==-1 && s->error); failure=0;
    /* A requested result from a pre-discontinuity generation cannot publish. */
    s->job_generation=atomic_load(&s->generation); s->job_slot=1005;
    js8_live_reset(s); atomic_store(&s->job_state,2); s->decoded_count=1;s->decoded[0]=d;
    unsigned before=s->processed; assert(!js8_live_publish(s) && s->processed==before);
    js8_live_destroy(s);free(s); assert(!allocations && !file_handles);
}
int main(void)
{
    timing_tests(); delay_tests(); semantic_tests();
    char *args[]={"js8chat","--rx","fake","--dial-hz","14078000","--cat","serial:fake","--log","/flash/log","--slots","2"};
    for (unsigned mode=0;mode<9;++mode) {
        failure=mode<8?mode:0; quit_after=mode==8?5:0; reads=total_frames=starts=stops=utc_queries=syncs=0;
        cat_size=0; memset(cat,0,sizeof(cat));
        int result=js8chat_run(&api,11,args,&worker);
        assert(result==((mode==0 || mode==3 || mode==6 || mode==8)?0:1));
        assert(!allocations && !audio_handles && !serial_handles && !file_handles && !working);
        if (mode==0) { assert(starts==1 && stops==1 && syncs==2 && utc_queries==reads); assert(!strcmp(cat,"MD6;FR0;FT0;FA00014078000;")); }
    }
    failure=0;quit_after=0;reads=total_frames=0;worker_delay=2150;
    char *plain[]={"js8chat","--rx","fake"};
    assert(!js8chat_run(&api,3,plain,&worker)); assert(!allocations);
    worker_delay=0; failure=3; reads=total_frames=0;
    char *delayed[]={"js8chat","--rx","fake","--rx-delay-ms","750","--slots","1"};
    assert(!js8chat_run(&api,7,delayed,&worker) && !allocations && !audio_handles);
    failure=0;
    mini_audio_api_t bad=audio; bad.capabilities=0;api.audio=&bad;
    assert(js8chat_run(&api,3,plain,&worker)==1);api.audio=&audio;
    char *invalid[]={"js8chat","--rx","fake","--cat","serial:fake"};
    assert(js8chat_run(&api,5,invalid,&worker)==2);
    char *duplicate[]={"js8chat","--rx","fake","--rx","fake"};
    assert(js8chat_run(&api,5,duplicate,&worker)==2);
    printf("js8_live_test: phase, UTC drift, slots, reset, services/CAT/FS/quit: PASS state=%zu\n",sizeof(Js8Live));
    return 0;
}

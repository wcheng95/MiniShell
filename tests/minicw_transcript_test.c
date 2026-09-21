#define main accepted_runtime_scenarios
#include "minicw_runtime_test.c"
#undef main
#include "../apps/minicw/src/port/minicw_port.c"
#include "../apps/minicw/src/app_core/app_core.c"
#include "../apps/minicw/src/app_core/transcript.c"

static int64_t utc_seconds;
static bool utc_fail;
static mini_result_t log_utc(mini_utc_time_t *out)
{
    out->unix_seconds=utc_seconds; return utc_fail?MINI_ERR_NOT_READY:MINI_OK;
}
static void *allocations[32];
static unsigned alloc_count, alloc_calls, alloc_fail, peak_alloc;
static mini_result_t queue_alloc(uint32_t bytes,void **out)
{
    *out=NULL; if (++alloc_calls==alloc_fail) return MINI_ERR_NO_MEMORY;
    for (unsigned i=0;i<32;++i) if (!allocations[i]) {
        allocations[i]=malloc(bytes); assert(allocations[i]); *out=allocations[i];
        ++alloc_count; if(alloc_count>peak_alloc) peak_alloc=alloc_count; return MINI_OK;
    }
    assert(false); return MINI_ERR_NO_MEMORY;
}
static mini_result_t queue_resize(void *old,uint32_t bytes,void **out)
{
    (void)old;(void)bytes;(void)out; assert(false); return MINI_ERR_NO_MEMORY;
}
static mini_result_t queue_free(void *ptr)
{
    for(unsigned i=0;i<32;++i) if(allocations[i]==ptr) {
        free(ptr); allocations[i]=NULL; --alloc_count; return MINI_OK;
    }
    assert(false); return MINI_ERR_INVALID;
}
static const mini_memory_api_t queue_memory={.struct_size=sizeof(queue_memory),.alloc=queue_alloc,.realloc=queue_resize,.free=queue_free};
static char daily[2][16000];
static bool log_live;
static unsigned daily_index, log_opens, log_writes, log_syncs, log_closes;
static const char *log_fail;
static uint32_t chunk_limit;
static bool failure(const char *op){return log_fail && !strcmp(log_fail,op);}
static bool shutdown_append;
static mini_result_t log_open(const char *path,uint32_t flags,mini_file_t *out)
{
    if(flags!=(MINI_FS_WRITE|MINI_FS_CREATE|MINI_FS_APPEND)) return fs_open(path,flags,out);
    assert(!log_live && !fs_live); ++log_opens;
    if (shutdown_append) assert(s_tone==MINI_AUDIO_TONE_INVALID);
    assert(!strcmp(path,"/flash/minicw/20260921.txt") || !strcmp(path,"/flash/minicw/20260922.txt"));
    daily_index=!strcmp(path,"/flash/minicw/20260922.txt");
    if(failure("open")) return MINI_ERR_IO;
    log_live=true; *out=88; return MINI_OK;
}
static mini_result_t log_write(mini_file_t f,const void *buf,uint32_t n,uint32_t *put)
{
    if(f!=88)return fs_write(f,buf,n,put);
    assert(log_live); ++log_writes;
    if(failure("write") || (failure("late_write") && log_writes>1))return MINI_ERR_IO;
    if(failure("zero")){*put=0;return MINI_OK;}
    if(failure("oversize")){*put=n+1;return MINI_OK;}
    if(n>chunk_limit)n=chunk_limit;
    size_t len=strlen(daily[daily_index]); assert(len+n<sizeof(daily[0]));
    memcpy(daily[daily_index]+len,buf,n);daily[daily_index][len+n]=0;*put=n;return MINI_OK;
}
static mini_result_t log_sync(mini_file_t f)
{
    if(f!=88)return fs_sync(f);
    assert(log_live);++log_syncs;return failure("sync")?MINI_ERR_IO:MINI_OK;
}
static mini_result_t log_close(mini_file_t f)
{
    if(f!=88)return fs_close(f);
    assert(log_live);log_live=false;++log_closes;return failure("close")?MINI_ERR_IO:MINI_OK;
}
static mini_result_t log_mkdir(const char *path)
{
    if(failure("mkdir"))return MINI_ERR_IO;
    return fs_mkdir(path);
}
static bool tone_opened;
static unsigned tone_enqueues,tone_stops,tone_holds;
static bool tone_is_busy;
static mini_result_t tone_open(mini_audio_tone_config_t const *c,mini_audio_tone_t *h){(void)c;tone_opened=true;*h=1;return MINI_OK;}
static mini_result_t tone_config(mini_audio_tone_t h,mini_audio_tone_config_t const *c){(void)h;(void)c;return MINI_OK;}
static mini_result_t tone_enqueue(mini_audio_tone_t h,uint32_t ms)
{
    assert(h==1 && ms); ++tone_enqueues; tone_is_busy=true; return MINI_OK;
}
static mini_result_t tone_hold(mini_audio_tone_t h,uint32_t hold){(void)h; ++tone_holds;tone_is_busy=hold!=0;return MINI_OK;}
static mini_result_t tone_stop(mini_audio_tone_t h)
{
    (void)h; if(s_note_active) assert(keyer_service_get_key_out_mode()==KEYER_KEY_OUT_OFF);
    ++tone_stops;tone_is_busy=false;return MINI_OK;
}
static mini_result_t tone_busy(mini_audio_tone_t h,uint32_t *busy){(void)h;*busy=tone_is_busy;return MINI_OK;}
static mini_result_t tone_close(mini_audio_tone_t h){tone_opened=false;return tone_stop(h);}
static int pending_char, pending_special;
static mini_result_t log_input(mini_key_event_t *key,uint32_t timeout)
{
    (void)timeout;
    if(pending_special){int code=pending_special;pending_special=0;return special(key,(uint32_t)code);}
    if(pending_char){int c=pending_char;pending_char=0;return ch(key,(char)c);}
    return MINI_ERR_NOT_READY;
}
static mini_result_t note_write(mini_digital_io_t h,uint32_t level)
{
    if(s_note_active && keyer_service_get_key_out_mode()==KEYER_KEY_OUT_OFF) assert(level==1);
    if(s_note_active && keyer_service_get_key_out_mode()!=KEYER_KEY_OUT_OFF)
        assert(!keyer_service_is_tx_active() && !keyer_service_tx_has_text() && !tone_is_busy);
    return io_write(h,level);
}
static mini_time_location_api_t log_time;
static mini_fs_api_t log_fs;
static mini_digital_io_api_t log_io;
static const mini_key_input_api_t log_keys={.struct_size=sizeof(log_keys),.read=log_input};
static mini_input_api_t log_in;
static const mini_audio_tone_api_t log_tone={sizeof(log_tone),tone_open,tone_config,tone_enqueue,tone_hold,tone_stop,tone_busy,tone_close};
static const mini_audio_api_t log_audio={.struct_size=sizeof(log_audio),.capabilities=MINI_AUDIO_CAP_TONE,.tone=&log_tone};
static void setup(void)
{
    assert(!alloc_count && !log_live);reset();
    memset(daily,0,sizeof(daily));alloc_calls=alloc_fail=peak_alloc=0;
    shutdown_append=false;
    log_opens=log_writes=log_syncs=log_closes=0;log_fail=NULL;chunk_limit=3;
    tone_enqueues=tone_stops=tone_holds=0;tone_is_busy=false;
    utc_seconds=1790016120;utc_fail=false;pending_char=pending_special=0;
    log_time=time_api;log_time.capabilities=MINI_TIMELOC_CAP_UTC;log_time.utc_get=log_utc;
    log_fs=fs_api;log_fs.open=log_open;log_fs.write=log_write;log_fs.sync=log_sync;log_fs.close=log_close;log_fs.mkdir=log_mkdir;
    log_io=io_api;log_io.write=note_write;log_in=input_api;log_in.key=&log_keys;
    api.fs=&log_fs;api.memory=&queue_memory;api.time_location=&log_time;api.audio=&log_audio;api.input=&log_in;api.digital_io=&log_io;
    s_api=&api;s_error=MINI_OK;s_exit=false;s_have_frame=false;
    for(unsigned i=0;i<4;++i){live[i]=true;s_lines[i]=i+1;levels[i]=1;}
    app_core_init();assert(s_error==MINI_OK);
}
static void finish(void)
{
    app_core_shutdown();minicw_port_tone_close();app_core_finish_transcript(true);app_core_save_on_exit();
    assert(!alloc_count && !log_live && !fs_live && s_error==MINI_OK);
    for(unsigned i=0;i<4;++i){live[i]=false;s_lines[i]=0;}s_api=NULL;
}
static void send_char(char c){pending_char=c;app_core_step();}
static void tick(unsigned ms){now_us+=ms*1000U;app_core_step();}
static void plain_capture(void)
{
    setup();
    keyer_event_t e={.type=KEYER_EVENT_CHAR_COMPLETE,.decoded_char='C'};
    app_core_handle_keyer_mode_decoded_event(&e);e.decoded_char='Q';app_core_handle_keyer_mode_decoded_event(&e);
    e.type=KEYER_EVENT_WORD_SPACE;app_core_handle_keyer_mode_decoded_event(&e);
    e.type=KEYER_EVENT_BACKSPACE;app_core_handle_keyer_mode_decoded_event(&e);
    transcript_append('\n');assert(!strcmp(s_payload,"CQ "));
    keyer_config_t cfg;keyer_service_get_config_copy(&cfg);cfg.tx_delay_s=99;
    for(unsigned i=0;i<5;++i){cfg.message[i][0]=(char)('A'+i);cfg.message[i][1]=0;}
    keyer_service_set_config(&cfg);
    send_char('x');send_char('y');send_char('\b');assert(!strcmp(s_payload,"CQ X"));
    for(unsigned i=1;i<=5;++i)app_core_keyer_append_message((uint8_t)i);
    assert(!strcmp(s_payload,"CQ X A B C D E"));
    keyer_service_tx_clear();s_keyer.tx_pending=false;s_keyer.m1_repeat_active=true;s_keyer.m1_repeat_waiting=true;
    s_keyer.m1_repeat_due=minicw_port_ticks();app_core_keyer_repeat_update();
    assert(!strcmp(s_payload,"CQ X A B C D EA"));
    finish();assert(!strcmp(daily[0],"1842 CQ X A B C D EA\n"));assert(log_syncs==1 && log_closes==1 && log_writes>1);
}
static void minute_queue(void)
{
    setup();strcpy(daily[0],"existing\n");utc_seconds=1790035140;
    transcript_text("OLD");s_keyer.tx_pending=true;s_keyer.tx_due=minicw_port_ticks()+100000;
    utc_seconds+=60;tick(10);assert(!log_writes && alloc_count==1);
    transcript_text("NEW");utc_seconds+=60;tick(10);transcript_text("THIRD");
    assert(alloc_count==2 && !log_writes);
    s_keyer.tx_pending=false;tick(300);assert(alloc_count==1);
    /* Input becoming active between records postpones the next append. */
    input_tip=0;tick(10);assert(alloc_count==1);
    input_tip=1;keyer_service_set_tune_active(false);tone_is_busy=false;
    tick(300);assert(alloc_count==0);
    assert(!strcmp(daily[0],"existing\n2359 OLD\n") && !strcmp(daily[1],"0000 NEW\n"));
    finish();assert(!strcmp(daily[1],"0000 NEW\n0001 THIRD\n"));
    setup();transcript_text("FIRST");utc_seconds+=60;transcript_update();
    transcript_text("DROP");alloc_fail=alloc_calls+1;utc_seconds+=60;transcript_update();
    assert(alloc_count==1);transcript_text("LAST");finish();assert(!strcmp(daily[0],"1842 FIRST\n1844 LAST\n"));
}
static void bounds_and_time(void)
{
    setup();for(unsigned i=0;i<1024;++i)transcript_append('A');assert(s_length==1024 && !s_truncated);
    transcript_finalize();transcript_drain(true);assert(strlen(daily[0])==1030 && !strstr(daily[0],"TRUNC"));
    for(unsigned i=0;i<1026;++i)transcript_append('B');
    transcript_backspace();transcript_append('C');
    assert(s_length==1024 && s_payload[1023]=='C');finish();
    char *suffix=strstr(daily[0]," [TRUNC]");assert(suffix && !strstr(suffix+1," [TRUNC]"));
    setup();for(unsigned i=0;i<1023;++i)transcript_append('A');app_core_note_toggle();assert(s_length==1024 && s_truncated);finish();
    setup();utc_fail=true;send_char('A');assert(!s_active);finish();assert(!*daily[0]);
    setup();uint32_t date;uint16_t minute;
    utc_seconds=951782400;assert(minicw_port_utc_minute(&date,&minute) && date==20000229 && minute==0);
    utc_seconds=-1;assert(minicw_port_utc_minute(&date,&minute) && date==19691231 && minute==1439);
    finish();
}
static void append_failures(void)
{
    const char *faults[]={"mkdir","open","write","zero","oversize","sync","close","late_write"};
    for(unsigned i=0;i<8;++i){setup();log_fail=faults[i];transcript_text("DATA");transcript_finalize();
        transcript_drain(true);unsigned opens=log_opens,close=log_closes;transcript_drain(true);
        assert(log_opens==opens && log_closes==close && !alloc_count && !log_live && s_error==MINI_OK);
        assert(log_closes==(i<2?0U:1U));finish();}
}
static void notes(void)
{
    const char quotes[2]={'\'', '"'};
    for(unsigned i=0;i<2;++i){setup();keyer_service_set_mute(true);
        send_char(quotes[i]);assert(s_note_active && s_note_key_out==KEYER_KEY_OUT_SK && s_note_mute);
        assert(keyer_service_get_key_out_mode()==KEYER_KEY_OUT_OFF && !keyer_service_get_mute());
        assert(!strcmp(s_payload,"**") && !keyer_service_tx_has_text() && !tone_enqueues);
        send_char('2');send_char('0');send_char('M');tick(20);assert(tone_enqueues && levels[2]==1 && levels[3]==1);
        ui_input_event_t change={.setting=UI_SETTING_KEYER_MUTE,.value=1};
        app_core_handle_keyer_mute_changed(&change);app_core_handle_key_out_mode_changed(&change);
        assert(!keyer_service_get_mute() && keyer_service_get_key_out_mode()==KEYER_KEY_OUT_OFF);
        storage_snapshot_t snap;app_core_snapshot(&snap);assert(snap.keyer.key_out_mode==KEYER_KEY_OUT_SK);
        char settings[1024];assert(storage_serialize(&snap,settings,sizeof(settings)) && strstr(settings,"key_out=SK-Normal\n"));
        unsigned stopped=tone_stops;send_char(quotes[1-i]);assert(tone_stops>stopped && !s_note_active);
        assert(!keyer_service_is_tx_active() && !keyer_service_tx_has_text() && !s_keyer.tx_pending && !tone_is_busy);
        assert(keyer_service_get_key_out_mode()==KEYER_KEY_OUT_SK && keyer_service_get_mute());
        assert(!strcmp(s_payload,"**20M**"));for(unsigned t=0;t<30;++t)tick(10);
        assert(levels[2]==1 && levels[3]==1);finish();assert(!strcmp(daily[0],"1842 **20M**\n"));
    }
    setup();keyer_service_set_mute(true);send_char('"');send_char('A');
    audio_service_set_volume(70);finish();assert(!strcmp(daily[0],"1842 **A**\n"));
    assert(s_settings.keyer.key_out_mode==KEYER_KEY_OUT_SK && keyer_service_get_mute());
    assert(strstr(fs_destination,"key_out=SK-Normal\n"));
    setup();send_char('\'');utc_seconds+=60;send_char('A');send_char('"');finish();
    assert(!strcmp(daily[0],"1842 **\n1843 A**\n"));
}
static void busy_guards(void)
{
    for(unsigned which=0;which<8;++which){setup();keyer_service_set_mute(true);
        switch(which){case 0:s_keyer.tx_pending=true;s_keyer.tx_due=minicw_port_ticks()+1000;break;
        case 1:keyer_service_tx_append_text("A",false);break;
        case 2:keyer_service_tx_append_text("A",false);keyer_service_tx_start();break;
        case 3:s_keyer.m1_repeat_active=true;break;
        case 4:s_keyer.m1_repeat_waiting=true;break;
        case 5:app_core_keyer_set_tune_active(true);break;
        case 6:tone_is_busy=true;break;
        case 7:input_tip=0;break;}
        send_char('\'');assert(!s_note_active && !s_length && keyer_service_get_mute() && keyer_service_get_key_out_mode()==KEYER_KEY_OUT_SK);
        transcript_text("BUSY");utc_seconds+=60;tick(10);assert(!log_writes && alloc_count==1);
        input_tip=1;finish();
    }
    setup();pending_special=MINI_KEY_OPT;app_core_step();send_char('"');assert(!s_note_active && !s_length);finish();
}
static void muted_manual(void)
{
    setup(); keyer_service_set_mute(true); input_tip=0; tick(10);
    input_tip=1; tick(10);
    assert(!tone_is_busy && keyer_service_has_manual_work());
    send_char('"'); assert(!s_note_active && !s_length);
    transcript_text("P"); utc_seconds+=60; tick(10);
    assert(!log_writes && alloc_count==1); finish();
}
static unsigned runtime_input_count;
static mini_result_t runtime_input(mini_key_event_t *key,uint32_t timeout)
{
    (void)timeout;
    const char keys[]={'"','2','0','M',3};
    assert(runtime_input_count<sizeof(keys)); return ch(key,keys[runtime_input_count++]);
}
static void runtime_shutdown(void)
{
    for(unsigned i=0;i<3;++i) {
        setup(); finish(); reset();
        memset(daily,0,sizeof(daily)); shutdown_append=true; runtime_input_count=0;
        if(i){strcpy(fs_csv,"K6ABC,Alice\n");fs_csv_exists=true;}
        mini_key_input_api_t keys=log_keys; keys.read=runtime_input; log_in.key=&keys;
        fail_sleep=i==2;
        assert(minicw_run(&api)==(i==2?1:0) && closes==4 && !alloc_count && !tone_opened && !log_live);
        if (i==2) assert(!*daily[0]);
        else assert(!strcmp(daily[0],"1842 **20M**\n"));
        fail_sleep=false;
    }
}
int main(void)
{
    plain_capture();minute_queue();bounds_and_time();append_failures();notes();busy_guards();muted_manual();runtime_shutdown();
    assert(!alloc_count);puts("Mini-CW transcript/note mode: PASS");return 0;
}

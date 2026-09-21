#define main accepted_runtime_scenarios
#include "minicw_runtime_test.c"
#undef main
#include "../apps/minicw/src/port/minicw_port.c"
#include "../apps/minicw/src/app_core/app_core.c"
static void bind(void)
{
    reset(); s_api=&api; s_error=MINI_OK; s_exit=false; s_have_frame=false;
    for (unsigned i=0;i<4;++i) { live[i]=true; s_lines[i]=i+1; levels[i]=1; }
}
static void unbind(void)
{
    for (unsigned i=0;i<4;++i) { live[i]=false; s_lines[i]=0; }
    s_api=NULL;
}

static int64_t utc_seconds;
static bool utc_failure;
static mini_result_t utc(mini_utc_time_t *out)
{
    assert(out->struct_size==sizeof(*out)); out->unix_seconds=utc_seconds;
    return utc_failure ? MINI_ERR_IO : MINI_OK;
}
static uint32_t pending_key;
static mini_result_t ui_input(mini_key_event_t *key,uint32_t timeout)
{
    assert(timeout==MINI_WAIT_NONE);
    if (!pending_key) return MINI_ERR_NOT_READY;
    uint32_t code=pending_key; pending_key=0;
    return special(key,code);
}
static void send_key(uint32_t code) { pending_key=code; app_core_step(); }
static void headers(void)
{
    mini_time_location_api_t tm=time_api; tm.capabilities=MINI_TIMELOC_CAP_UTC; tm.utc_get=utc;
    mini_key_input_api_t keys={.struct_size=sizeof(keys),.read=ui_input};
    mini_input_api_t input=input_api; input.key=&keys;
    api.time_location=&tm; api.input=&input;
    bind(); app_core_init();
    assert(!strcmp(frame[0],"00:00 PDN SKN 19 V80"));
    send_key(MINI_KEY_CTRL); assert(frame[1][0]==' ');
    send_key(MINI_KEY_OPT); assert(!strncmp(frame[1],"1 Vol:",6));
    assert(!strcmp(frame[0],"00:00 PDN SKN 19 V80"));
    send_key(MINI_KEY_CTRL); assert(!strncmp(frame[1],"1 Vol:",6));
    assert(!strncmp(frame[5],"5 In:Paddle-Normal",18) && !strncmp(frame[6],"6 Out:SK-Normal",14));
    send_key(MINI_KEY_OPT); assert(frame[1][0]==' ');
    static const char *const ins[]={"PDN","PDR","SKT","SKR","SKB"};
    static const char *const outs[]={"SKN","SKM","OFF"};
    audio_service_set_volume(5); keyer_service_set_key_in_wpm(5); keyer_service_set_sk_wpm(27);
    utc_seconds=23*3600+59*60;
    for (unsigned i=0;i<5;++i) for (unsigned o=0;o<3;++o) {
        keyer_service_set_key_in_mode((keyer_key_in_mode_t)i);
        keyer_service_set_key_out_mode((keyer_key_out_mode_t)(o+KEYER_KEY_OUT_SK));
        char expected[21]; snprintf(expected,sizeof(expected),"23:59 %s %s %s V05",ins[i],outs[o],i<2?"05":"27");
        ui_service_refresh(); assert(strlen(frame[0])==20 && !strcmp(frame[0],expected));
        send_key(MINI_KEY_OPT); assert(!strcmp(frame[0],expected));
        send_key(MINI_KEY_DOWN); assert(!strcmp(frame[0],expected));
        send_key(MINI_KEY_DOWN); assert(!strcmp(frame[0],expected));
        send_key(MINI_KEY_OPT);
    }
    utc_seconds=5*60; app_core_step(); assert(!strncmp(frame[0],"00:05",5));
    utc_seconds=9*3600+37*60; ui_service_refresh(); assert(!strncmp(frame[0],"09:37",5));
    utc_failure=true; ui_service_refresh(); assert(!strncmp(frame[0],"--:--",5) && s_error==MINI_OK);
    utc_failure=false; tm.capabilities=0; ui_service_refresh(); assert(!strncmp(frame[0],"--:--",5));
    tm.capabilities=MINI_TIMELOC_CAP_UTC; tm.utc_get=NULL;
    audio_service_set_volume(0); ui_service_refresh();
    assert(!strncmp(frame[0],"--:--",5) && !strcmp(frame[0]+17,"V00"));
    audio_service_set_volume(99); ui_service_refresh(); assert(!strcmp(frame[0]+17,"V99"));
    app_core_shutdown(); unbind(); api.time_location=&time_api; api.input=&input_api;
}
static void compatibility(void)
{
    static const char *const in[]={"Pdl","Paddle","Pdl-R","PdlR","PaddleR","Paddle-R","Paddle_R","Paddle_Reverse","SK-T","SKT","SK-R","SKR"};
    const unsigned modes[]={0,0,1,1,1,1,1,1,2,2,3,3};
    storage_snapshot_t s,again;
    char text[1024];
    for (unsigned i=0;i<sizeof(modes)/sizeof(modes[0]);++i) {
        snprintf(text,sizeof(text),"[system]\nkey_in=%s",in[i]); assert(storage_parse(text,&s) && s.key_in==(keyer_key_in_mode_t)modes[i]);
    }
    static const char *const out[]={"SK","SK-M","SKM","SK-Mono","OFF","Pdl","Paddle","Pdl-R","PaddleR","0","1","2","3","4"};
    const unsigned om[]={2,3,3,3,4,2,2,2,2,2,2,2,3,4};
    for (unsigned i=0;i<sizeof(om)/sizeof(om[0]);++i) {
        snprintf(text,sizeof(text),"[keyer]\nkey_out=%s",out[i]); assert(storage_parse(text,&s) && s.keyer.key_out_mode==(keyer_key_out_mode_t)om[i]);
    }
    for (unsigned i=0;i<5;++i) for (unsigned o=2;o<5;++o) {
        storage_defaults(&s); s.key_in=(keyer_key_in_mode_t)i; s.keyer.key_out_mode=(keyer_key_out_mode_t)o;
        assert(storage_serialize(&s,text,sizeof(text)) && storage_parse(text,&again) && storage_equal(&s,&again));
        assert(strstr(text,keyer_service_key_in_mode_label(s.key_in)) && strstr(text,keyer_service_key_out_mode_label(s.keyer.key_out_mode)));
    }
}
int main(void) { headers(); compatibility(); puts("Mini-CW T045 UI/I/O: PASS"); return 0; }

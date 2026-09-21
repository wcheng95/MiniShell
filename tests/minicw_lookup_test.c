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
static keyer_op_entry_t entries[MINICW_OP_ENTRY_CAP];
static void csv_cases(void)
{
    bind(); size_t count=123;
    assert(storage_op_load(entries,&count)==STORAGE_OP_MISSING && count==0 && !fs_writes);
    strcpy(fs_csv,"call,name\r\n\n#comment\n ; comment\n k6abc , Alice \nW1XYZ,Bob\nK6ABC,Second\n"
        "badrow\nK1A,Two,Names\nK/1A,Bad\nLONG123,Bad\nK1A,TooLongName12\nK1A,\n,Name\nK1A,Bad\tName\nK1B,Bad\177Name\nK1C,Name Here\n");
    fs_csv_exists=true;
    assert(storage_op_load(entries,&count)==STORAGE_OP_OK && count==4 && fs_csv_reads>1);
    assert(!strcmp(entries[0].call,"K6ABC") && !strcmp(entries[0].name,"Alice"));
    assert(!strcmp(entries[1].call,"W1XYZ") && !strcmp(entries[1].name,"Bob"));
    assert(!strcmp(entries[2].name,"Second") && !strcmp(entries[3].name,"Name Here"));
    const char *faults[]={"open_read","read","read_late","close_read"};
    for (unsigned i=0;i<4;++i) {
        fs_fail=faults[i]; fs_reads=0; count=123;
        assert(storage_op_load(entries,&count)==STORAGE_OP_FAILED && count==0 && !fs_live);
    }
    fs_fail=NULL; memset(fs_csv,'X',5000); fs_csv[5000]=0;
    assert(storage_op_load(entries,&count)==STORAGE_OP_FAILED && count==0);
    memset(fs_csv,'X',200); strcpy(fs_csv+200,"\nK6ABC,Alice\n");
    assert(storage_op_load(entries,&count)==STORAGE_OP_OK && count==1);
    fs_csv[0]=0;
    for (unsigned i=0;i<193;++i) { char line[32]; snprintf(line,sizeof(line),"K%u,N%u\n",i,i); strcat(fs_csv,line); }
    assert(storage_op_load(entries,&count)==STORAGE_OP_TRUNCATED && count==192);
    assert(!strcmp(entries[191].call,"K191") && !strcmp(entries[191].name,"N191"));
    assert(!fs_writes && !fs_attempts); unbind();
}
static const keyer_op_entry_t domain_table[]={
    {"K6ABC","Alice"},{"K6ABC","Second"},{"W1XYZ","Bob"},{"AG6AQ","Own"},{"HELLO","NoDigit"}};
static void lookup(const char *text,const char *name)
{
    keyer_service_clear_op_name(); keyer_service_op_feed_text(text);
    assert(!strcmp(keyer_service_get_op_name(),name));
}
static void domain_and_ui(void)
{
    bind(); app_core_init(); keyer_service_set_op_table(domain_table,5);
    lookup("K6ABC ","Alice"); lookup("K6ABC/P ","Alice"); lookup("F/K6ABC ","Alice");
    lookup("AG6AQ ",""); lookup("AG6AQ/P ",""); lookup("HELLO ",""); lookup("W9ZZZ ","");
    lookup("W1XYZ ","Bob"); keyer_service_op_feed_text("72 "); assert(!*keyer_service_get_op_name());
    lookup("K6ABC ","Alice"); keyer_service_op_feed_text("73 "); assert(!*keyer_service_get_op_name());
    lookup("K6ABC ","Alice"); ui_service_refresh();
    assert(!strcmp(frame[0],"--:-- PDN SKN 19 V80") && !strncmp(frame[6],"OP:Alice",8));
    ui_service_keyer_set_tx_text("CQ K6ABC"); ui_service_refresh(); assert(!strncmp(frame[6],"CQ K6ABC",8));
    ui_service_keyer_set_status("Status"); ui_service_refresh(); assert(!strncmp(frame[6],"Status",6));
    app_core_keyer_set_tune_active(true); assert(!strncmp(frame[6],"Tune",4) && !*keyer_service_get_op_name());
    assert(!strcmp(frame[0],"--:-- PDN SKN 19 V80"));
    app_core_keyer_set_tune_active(false); lookup("K6ABC ","Alice");
    ui_service_keyer_set_tx_text(""); ui_service_refresh(); assert(!strncmp(frame[6],"Status",6));
    now_us+=1300000;
    /* Real idle poll must reveal OP when transient status expires. */
    reads=10; app_core_step(); assert(!strncmp(frame[6],"OP:Alice",8));
    keyer_service_clear_op_name(); ui_service_refresh(); assert(frame[6][0]==' ');
    assert(!strcmp(frame[0],"--:-- PDN SKN 19 V80"));
    keyer_service_set_op_table(NULL,500); lookup("K6ABC ","");
    app_core_shutdown(); unbind();
}
static unsigned tone_opened, csv_finished_reads;
static void before_csv(void) { assert(!tone_opened); }
static mini_result_t tone_open_observer(const mini_audio_tone_config_t *c,mini_audio_tone_t *h)
{
    (void)c; assert(!fs_live && fs_csv_opens==1 && fs_csv_reads && fs_closes==1);
    csv_finished_reads=fs_csv_reads; ++tone_opened; *h=1; return MINI_OK;
}
static mini_result_t tone_config(mini_audio_tone_t h,const mini_audio_tone_config_t *c) { (void)c; assert(h==1); return MINI_OK; }
static mini_result_t tone_action(mini_audio_tone_t h) { assert(h==1); return MINI_OK; }
static mini_result_t tone_value(mini_audio_tone_t h,uint32_t v) { (void)v; return tone_action(h); }
static mini_result_t tone_busy_observer(mini_audio_tone_t h,uint32_t *out) { *out=0; return tone_action(h); }
static mini_result_t input_exit(mini_key_event_t *key,uint32_t timeout)
{
    (void)timeout; assert(tone_opened==1 && fs_csv_reads==csv_finished_reads && fs_csv_opens==1);
    lookup("K6ABC ","Alice"); ui_service_refresh(); assert(!strncmp(frame[6],"OP:Alice",8));
    return ch(key,3);
}
static void startup(void)
{
    mini_audio_tone_api_t tone={sizeof(tone),tone_open_observer,tone_config,tone_value,tone_value,tone_action,tone_busy_observer,tone_action};
    mini_audio_api_t audio={.struct_size=sizeof(audio),.capabilities=MINI_AUDIO_CAP_TONE,.tone=&tone};
    mini_key_input_api_t keys={.struct_size=sizeof(keys),.read=input_exit};
    mini_input_api_t in=input_api; in.key=&keys;
    for (unsigned i=0;i<2;++i) {
        reset(); strcpy(fs_csv,"K6ABC,Alice\n"); fs_csv_exists=true; fs_before_csv=before_csv;
        tone_opened=0; api.audio=&audio; api.input=&in;
        assert(minicw_run(&api)==0 && closes==4 && !fs_writes && fs_csv_opens==1 && fs_csv_reads==csv_finished_reads);
    }
    api.audio=NULL; api.input=&input_api;
    bind(); for (unsigned i=0;i<193;++i) strcat(fs_csv,"K1A,Name\n"); fs_csv_exists=true;
    app_core_init(); assert(!strncmp(frame[6],"Lookup truncated",16)); app_core_shutdown(); unbind();
    bind(); fs_csv_exists=true; fs_fail="read"; strcpy(fs_csv,"K6ABC,Alice\n");
    app_core_init(); assert(s_error==MINI_OK && !*keyer_service_get_op_name() && !fs_writes);
    app_core_shutdown(); unbind();
}
int main(void) { csv_cases(); domain_and_ui(); startup(); puts("Mini-CW lookup: PASS"); return 0; }

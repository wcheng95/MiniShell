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
static keyer_op_entry_t *entries;
static storage_op_result_t load(size_t *count)
{
    storage_op_free(entries); entries = NULL;
    storage_op_result_t result = storage_op_load(&entries, count);
    if (result != STORAGE_OP_OK) assert(!entries && !*count && !memory_live);
    return result;
}
static void csv_cases(void)
{
    bind(); size_t count=123;
    assert(load(&count)==STORAGE_OP_MISSING && count==0 && !fs_writes);
    strcpy(fs_csv,"call,name\r\n\n#comment\n ; comment\n k6abc , Alice \nW1XYZ,Bob\nK6ABC,Second\n"
        "badrow\nK1A,Two,Names\nK/1A,Bad\nLONG123,Bad\nK1A,TooLongName12\nK1A,\n,Name\nK1A,Bad\tName\nK1B,Bad\177Name\nK1C,Name Here\n");
    fs_csv_exists=true;
    assert(load(&count)==STORAGE_OP_OK && count==4 && fs_csv_reads>1);
    assert(!strcmp(entries[0].call,"K6ABC") && !strcmp(entries[0].name,"Alice"));
    assert(!strcmp(entries[1].call,"W1XYZ") && !strcmp(entries[1].name,"Bob"));
    assert(!strcmp(entries[2].name,"Second") && !strcmp(entries[3].name,"Name Here"));
    const char *faults[]={"open_read","read","read_late","close_read"};
    for (unsigned i=0;i<4;++i) {
        fs_fail=faults[i]; fs_reads=0; count=123;
        assert(load(&count)==STORAGE_OP_FAILED && count==0 && !fs_live);
    }
    fs_fail=NULL; memset(fs_csv,'X',5000); fs_csv[5000]=0;
    assert(load(&count)==STORAGE_OP_OK && count==0);
    memset(fs_csv,'X',200); strcpy(fs_csv+200,"\nK6ABC,Alice\n");
    assert(load(&count)==STORAGE_OP_OK && count==1);
    fs_csv[0]=0;
    for (unsigned i=0;i<193;++i) { char line[32]; snprintf(line,sizeof(line),"K%u,N%u\n",i,i); strcat(fs_csv,line); }
    assert(load(&count)==STORAGE_OP_OK && count==193);
    assert(!strcmp(entries[191].call,"K191") && !strcmp(entries[191].name,"N191"));
    assert(!fs_writes && !fs_attempts); storage_op_free(entries); entries=NULL; unbind();
}
static void full_database(void)
{
    strcpy(fs_csv,"call,name\nK6ABC,Alice\n");
    for (unsigned i=0;i<812;++i) {
        char row[32]; snprintf(row,sizeof(row),"K%04u,Person%s\n",i,i<210 ? "X" : "");
        /* The app's minimal formatter space-pads numeric width: make the
         * deterministic fixture's calls alphanumeric with explicit zero fill. */
        for (unsigned j=1;j<5;++j) if (row[j]==' ') row[j]='0';
        if (i==191) strcpy(row,"K7SO,SAT     \n");
        strcat(fs_csv,row);
    }
    unsigned lines=0; for (const char *p=fs_csv;*p;++p) if (*p=='\n') ++lines;
    assert(strlen(fs_csv)==10788 && lines==814); fs_csv_exists=true;
}
static void stream_cases(void)
{
    bind(); full_database(); size_t count;
    const unsigned chunks[]={1,2,7,127,128,256};
    for (unsigned i=0;i<sizeof(chunks)/sizeof(chunks[0]);++i) {
        fs_read_limit=chunks[i];
        assert(load(&count)==STORAGE_OP_OK && count==813);
        assert(fs_position==10788 && !fs_live && !strcmp(entries[0].name,"Alice"));
        assert(!strcmp(entries[812].call,"K0811"));
        assert(memory_bytes==19456);
        keyer_service_set_op_table(entries,count);
        keyer_service_op_feed_text("K7SO "); assert(!strcmp(keyer_service_get_op_name(),"SAT"));
        keyer_service_op_feed_text("K0811 "); assert(!strcmp(keyer_service_get_op_name(),"Person"));
        keyer_service_set_op_table(NULL,0);
    }
    storage_op_free(entries); entries=NULL;
    for (unsigned fail=1;fail<=5;++fail) {
        memory_calls=0; memory_fail_at=fail;
        assert(load(&count)==STORAGE_OP_FAILED && count==0 && !fs_live && !memory_live);
        assert(memory_calls==fail);
    }
    memory_fail_at=0;
    /* Late failures discard all allocated rows. */
    fs_csv_nul_at=6000; assert(load(&count)==STORAGE_OP_FAILED && count==0 && !fs_live);
    fs_csv_nul_at=0; fs_csv_fail_at=6000;
    assert(load(&count)==STORAGE_OP_FAILED && count==0 && !fs_live);
    fs_csv_fail_at=0; fs_fail="close_read";
    assert(load(&count)==STORAGE_OP_FAILED && count==0 && !fs_live);
    fs_fail=NULL;
    /* Split CRLF, malformed lines, overlong lines and final unterminated record. */
    strcpy(fs_csv,"call,name\r\nK6ABC,Alice\r\nBad\r\n");
    size_t len=strlen(fs_csv); memset(fs_csv+len,'X',5000);
    strcpy(fs_csv+len+5000,"\r\nW1XYZ,Bob"); fs_read_limit=1;
    assert(load(&count)==STORAGE_OP_OK && count==2 && !strcmp(entries[1].name,"Bob"));
    /* Invalid rows consume no entries, including across the former cap. */
    fs_csv[0]=0;
    for (unsigned i=0;i<192;++i) strcat(fs_csv,"K1A,Name\n");
    strcat(fs_csv,"bad\nK/1A,Invalid\n");
    assert(load(&count)==STORAGE_OP_OK && count==192);
    strcat(fs_csv,"K2A,Last");
    assert(load(&count)==STORAGE_OP_OK && count==193);
    assert(!fs_writes && !fs_attempts); storage_op_free(entries); entries=NULL; unbind();
}
static const keyer_op_entry_t domain_table[]={
    {"K6ABC","Alice"},{"K6ABC","Second"},{"W1XYZ","Bob"},{"AG6AQ","Own"},{"HELLO","NoDigit"},{"K7SHR","PAUL"},{"7N1FRE","ABCDEFGHIJK"}};
static void lookup(const char *text,const char *name)
{
    keyer_service_clear_op_name(); keyer_service_op_feed_text(text);
    assert(!strcmp(keyer_service_get_op_name(),name));
    if (!*name) assert(!*keyer_service_get_op_call());
}
static void domain_and_ui(void)
{
    bind(); app_core_init(); keyer_service_set_op_table(domain_table,7);
    const char *variants[]={"K6ABC ","K6ABC/P ","F/K6ABC "};
    for (unsigned i=0;i<3;++i) {
        lookup(variants[i],"Alice"); assert(!strcmp(keyer_service_get_op_call(),"K6ABC"));
        ui_service_refresh(); assert(!strncmp(frame[6],"K6ABC: Alice",12));
    }
    /* Unknown candidates retain the last matched pair, never a new call/old name. */
    keyer_service_op_feed_text("W9ZZZ ");
    assert(!strcmp(keyer_service_get_op_call(),"K6ABC") && !strcmp(keyer_service_get_op_name(),"Alice"));
    lookup("K7SHR ","PAUL"); ui_service_refresh(); assert(!strncmp(frame[6],"K7SHR: PAUL",11));
    lookup("7N1FRE ","ABCDEFGHIJK"); ui_service_refresh();
    assert(!strcmp(frame[6],"7N1FRE: ABCDEFGHIJK "));
    lookup("AG6AQ ",""); lookup("AG6AQ/P ",""); lookup("HELLO ",""); lookup("W9ZZZ ","");
    lookup("W1XYZ ","Bob"); keyer_service_op_feed_text("72 "); assert(!*keyer_service_get_op_name() && !*keyer_service_get_op_call());
    lookup("K6ABC ","Alice"); keyer_service_op_feed_text("73 "); assert(!*keyer_service_get_op_name() && !*keyer_service_get_op_call());
    lookup("K6ABC ","Alice"); ui_service_refresh();
    assert(!strcmp(frame[0],"--:-- PDN SKN 19 V80") && !strncmp(frame[6],"K6ABC: Alice",12));
    ui_service_keyer_set_tx_text("CQ K6ABC"); ui_service_refresh(); assert(!strncmp(frame[6],"CQ K6ABC",8));
    ui_service_keyer_set_status("Status"); ui_service_refresh(); assert(!strncmp(frame[6],"Status",6));
    app_core_keyer_set_tune_active(true); assert(!strncmp(frame[6],"Tune",4) && !*keyer_service_get_op_name() && !*keyer_service_get_op_call());
    assert(!strcmp(frame[0],"--:-- PDN SKN 19 V80"));
    app_core_keyer_set_tune_active(false); lookup("K6ABC ","Alice");
    ui_service_keyer_set_tx_text(""); ui_service_refresh(); assert(!strncmp(frame[6],"Status",6));
    now_us+=1300000;
    /* Real idle poll must reveal OP when transient status expires. */
    reads=10; app_core_step(); assert(!strncmp(frame[6],"K6ABC: Alice",12));
    keyer_service_clear_op_name(); ui_service_refresh(); assert(frame[6][0]==' ');
    assert(!strcmp(frame[0],"--:-- PDN SKN 19 V80"));
    lookup("K6ABC ","Alice"); keyer_service_set_op_table(NULL,500);
    assert(!*keyer_service_get_op_call() && !*keyer_service_get_op_name()); lookup("K6ABC ","");
    keyer_service_set_op_table(domain_table,7); lookup("K6ABC ","Alice");
    keyer_service_init(); assert(!*keyer_service_get_op_call() && !*keyer_service_get_op_name());
    app_core_shutdown(); unbind();
}
static unsigned tone_opened, csv_finished_reads;
static void before_csv(void) { assert(!tone_opened); }
static mini_result_t tone_open_observer(const mini_audio_tone_config_t *c,mini_audio_tone_t *h)
{
    (void)c; assert(!fs_live && fs_csv_opens==1 && fs_csv_reads && fs_closes==1);
    assert(fs_position==strlen(fs_csv));
    csv_finished_reads=fs_csv_reads; ++tone_opened; *h=1; return MINI_OK;
}
static mini_result_t tone_config(mini_audio_tone_t h,const mini_audio_tone_config_t *c) { (void)c; assert(h==1); return MINI_OK; }
static mini_result_t tone_action(mini_audio_tone_t h) { assert(h==1); return MINI_OK; }
static mini_result_t tone_value(mini_audio_tone_t h,uint32_t v) { (void)v; return tone_action(h); }
static mini_result_t tone_busy_observer(mini_audio_tone_t h,uint32_t *out) { *out=0; return tone_action(h); }
static mini_result_t input_exit(mini_key_event_t *key,uint32_t timeout)
{
    (void)timeout; assert(tone_opened==1 && fs_csv_reads==csv_finished_reads && fs_csv_opens==1);
    if (strlen(fs_csv)==10788) {
        assert(strncmp(frame[6],"Lookup",6));
        lookup("K7SO ","SAT"); lookup("K0811 ","Person");
    }
    lookup("K6ABC ","Alice");
    now_us+=1300000; ui_service_refresh(); assert(!strncmp(frame[6],"K6ABC: Alice",12));
    return ch(key,3);
}
static void detached_before_free(void)
{
    lookup("K6ABC ","");
}
static void startup(void)
{
    mini_audio_tone_api_t tone={sizeof(tone),tone_open_observer,tone_config,tone_value,tone_value,tone_action,tone_busy_observer,tone_action};
    mini_audio_api_t audio={.struct_size=sizeof(audio),.capabilities=MINI_AUDIO_CAP_TONE,.tone=&tone};
    mini_key_input_api_t keys={.struct_size=sizeof(keys),.read=input_exit};
    mini_input_api_t in=input_api; in.key=&keys;
    for (unsigned i=0;i<4;++i) {
        reset(); if (i>=2) full_database(); else { strcpy(fs_csv,"K6ABC,Alice\n"); fs_csv_exists=true; } fs_before_csv=before_csv;
        tone_opened=0; api.audio=&audio; api.input=&in; memory_before_free=detached_before_free;
        assert(minicw_run(&api)==0 && closes==4 && !fs_writes && fs_csv_opens==1 && fs_csv_reads==csv_finished_reads);
        assert(!memory_live && memory_frees==1);
    }
    api.audio=NULL; api.input=&input_api;
    for (unsigned fail=1;fail<=5;++fail) {
        bind(); full_database(); memory_fail_at=fail;
        app_core_init();
        assert(s_error==MINI_OK && !memory_live && !fs_live && !s_op_table);
        assert(!strncmp(frame[6],"Lookup unavailable",18));
        app_core_shutdown(); unbind();
    }
    bind(); full_database(); api.memory=NULL;
    app_core_init(); assert(s_error==MINI_OK && !memory_live && !fs_live);
    assert(!strncmp(frame[6],"Lookup unavailable",18));
    app_core_shutdown(); api.memory=&memory_api; unbind();
    bind(); for (unsigned i=0;i<193;++i) strcat(fs_csv,"K1A,Name\n"); fs_csv_exists=true;
    app_core_init(); assert(strncmp(frame[6],"Lookup",6)); app_core_shutdown(); unbind();
    bind(); fs_csv_exists=true; fs_fail="read"; strcpy(fs_csv,"K6ABC,Alice\n");
    app_core_init(); assert(s_error==MINI_OK && !*keyer_service_get_op_name() && !fs_writes);
    app_core_shutdown(); unbind();
}
int main(void) { csv_cases(); stream_cases(); domain_and_ui(); startup(); puts("Mini-CW lookup: PASS"); return 0; }

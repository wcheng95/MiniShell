/* Real portable lifecycle, UI adapter/shell and CAT synchronizer; fake services
 * and controller storage/audio let us prove startup order deterministically. */
#include "app_controller.h"
#include "radio_control.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

static bool worker_start(AppController *app);
static void worker_stop(AppController *app);
#define FT8_PLATFORM_DECODE_WORKER_START(app) worker_start(app)
#define FT8_PLATFORM_DECODE_WORKER_STOP(app) worker_stop(app)
#define main ft8_lifecycle_entry
#include "../apps/ft8/main/ft8_main.c"
#undef main

struct AppController { RadioControl radio; bool rx; } controller;
static mini_api_t api;
static uint64_t now, ready_at, last_open;
static unsigned opens, writes, rx_starts, worker_starts, worker_stops, input_reads;
static unsigned presents, normal_frames, qso_actions, tx_steps, destroys, tone_tests;
static bool fail_rx, fail_worker, fixture, expect_cat, fail_render;
static mini_result_t cat_failure;
static char lcd[7][21], sent[128];
static unsigned seen;

static uint64_t clock_us(void) { return now; }
const mini_api_t *mini_api_get(void) { return &api; }
static void message(const char *text) { assert(!strstr(text,"waiting for QMX")); }
static mini_result_t memory_alloc(uint32_t size,void **out) { *out=malloc(size);return *out?MINI_OK:MINI_ERR_NO_MEMORY; }
static mini_result_t memory_free(void *p) { free(p);return MINI_OK; }
static mini_result_t info(mini_text_display_info_t *out) { out->columns=20;out->rows=7;return MINI_OK; }
static mini_result_t clear(void) { memset(lcd,' ',sizeof(lcd));for(unsigned r=0;r<7;++r)lcd[r][20]=0;return MINI_OK; }
static mini_result_t write_at(uint32_t row,uint32_t column,const char *text,uint32_t size)
{ assert(row<7 && column+size<=20);memcpy(lcd[row]+column,text,size);return MINI_OK; }
static mini_result_t present(void)
{
    ++presents;
    if(lcd[0][0]!=' '){++normal_frames;assert(!controller.rx || !expect_cat || writes==4);}
    for(unsigned row=0;row<7;++row)if(strstr(lcd[row],"No QSOs"))seen|=1;
    if(lcd[0][0]=='O')seen|=2;
    if(lcd[0][0]=='S')seen|=4;
    if(lcd[0][0]=='R')seen|=8;
    if(lcd[0][0]=='T')seen|=16;
    return fail_render?MINI_ERR_IO:MINI_OK;
}
static mini_result_t key_read(mini_key_event_t *event,uint32_t timeout)
{
    assert(normal_frames && input_reads<140);
    if(!controller.rx)assert(timeout==100);
    now+=100000; ++input_reads;
    static const char navigation[]={'v','3',0,'o','s','r','t','v','1',0,0,'r'};
    if(input_reads<=sizeof(navigation)) {
        char c=navigation[input_reads-1];
        event->type=c?MINI_KEY_EVENT_CHAR:MINI_KEY_EVENT_SPECIAL;
        event->codepoint=(uint32_t)c;event->key=c?0:MINI_KEY_ESCAPE;
        return MINI_OK;
    }
    if(input_reads==120) { event->type=MINI_KEY_EVENT_CHAR;event->codepoint='q';return MINI_OK; }
    return MINI_ERR_TIMEOUT;
}
static mini_result_t serial_open(const char *endpoint,mini_serial_t *out)
{
    assert(!strcmp(endpoint,"control:test") && normal_frames>0);
    *out=MINI_SERIAL_INVALID;
    if(opens)assert(now-last_open>=300000);
    last_open=now;++opens;
    if(now<ready_at)return MINI_ERR_NOT_READY;
    if(cat_failure!=MINI_OK)return cat_failure;
    *out=1;return MINI_OK;
}
static mini_result_t serial_write(mini_serial_t handle,const void *data,uint32_t count,uint32_t *out,uint32_t timeout)
{
    (void)timeout;assert(handle==1 && !rx_starts && !worker_starts);
    assert(strlen(sent)+count<sizeof(sent));strncat(sent,data,count);*out=count;++writes;return MINI_OK;
}
static mini_result_t serial_close(mini_serial_t handle) { assert(handle==1);return MINI_OK; }
AppController *app_controller_create(const mini_api_t *p,const char *data,const char *station)
{ assert(p==&api && data && station);memset(&controller,0,sizeof(controller));return &controller; }
void app_controller_destroy(AppController *app)
{ assert(app==&controller);++destroys;radio_control_close(&controller.radio);controller.rx=false; }
mini_result_t app_controller_start_cat(AppController *app,const char *endpoint)
{ return radio_control_open_qmx(&app->radio,&api,endpoint,7074000); }
bool app_controller_start_rx(AppController *app,const AppRxStartConfig *config)
{
    assert(app==&controller && normal_frames && !worker_starts && !rx_starts);
    if(expect_cat)assert(writes==4 && !strcmp(sent,"MD6;FR0;FT0;FA00007074000;"));
    assert(config->has_explicit_timing==fixture);
    if(fixture)assert(config->slot_id==123 && config->sample_offset==0);
    ++rx_starts;app->rx=!fail_rx;return !fail_rx;
}
static bool worker_start(AppController *app)
{ assert(app->rx && rx_starts==1 && !worker_starts);++worker_starts;return !fail_worker; }
static void worker_stop(AppController *app) { assert(app->rx && worker_starts==1);++worker_stops; }
bool app_controller_step_cat(AppController *app) { assert(!expect_cat || app->radio.stream);return true; }
bool app_controller_step_rx(AppController *app,bool *changed) { (void)app;*changed=false;return true; }
bool app_controller_step_location(AppController *app,bool *changed) { (void)app;(void)changed;return true; }
bool app_controller_rx_active(const AppController *app) { return app->rx; }
bool app_controller_tx_active(const AppController *app) { (void)app;return false; }
bool app_controller_step_tx(AppController *app,bool *changed)
{ (void)changed;assert(!expect_cat || app->radio.stream);++tx_steps;return true; }
void app_controller_step_qso(AppController *app) { (void)app; }
void app_controller_build_model(const AppController *app,UiModel *model)
{ memset(model,0,sizeof(*model));model->rx_active=app->rx;strcpy(model->band_name,"20");model->band_count=model->profile_count=1; }
bool app_controller_apply_action(AppController *app,const AppAction *action)
{ (void)app;if(action->type==APP_ACTION_LOAD_QSO_PAGE)++qso_actions;return true; }
mini_result_t app_controller_cat_test(const mini_api_t *p,const char *station,const char *endpoint,float tone,uint32_t duration)
{ assert(p==&api && station && endpoint && tone==1500 && duration==500);++tone_tests;return MINI_OK; }
static mini_time_location_api_t time_api={.struct_size=sizeof(time_api),.monotonic_us=clock_us};
static const mini_system_api_t system_api={.write=message};
static const mini_memory_api_t memory_api={.alloc=memory_alloc,.free=memory_free};
static const mini_fs_api_t fs={0};
static const mini_audio_api_t audio={0};
static const mini_serial_api_t serial_api={.struct_size=sizeof(serial_api),.capabilities=MINI_SERIAL_CAP_WRITE,.open=serial_open,.write=serial_write,.close=serial_close};
static const mini_text_display_api_t text_api={.struct_size=sizeof(text_api),.get_info=info,.clear=clear,.write_at=write_at};
static const mini_display_api_t display_api={.struct_size=sizeof(display_api),.capabilities=MINI_DISPLAY_CAP_TEXT,.text=&text_api,.present=present};
static const mini_key_input_api_t key_api={.struct_size=sizeof(key_api),.read=key_read};
static const mini_input_api_t input_api={.struct_size=sizeof(input_api),.capabilities=MINI_INPUT_CAP_KEY,.key=&key_api};
static void reset(void)
{
    now=last_open=ready_at=0;opens=writes=rx_starts=worker_starts=worker_stops=input_reads=0;
    presents=normal_frames=qso_actions=tx_steps=destroys=tone_tests=seen=0;sent[0]=0;
    fail_rx=fail_worker=fixture=fail_render=false;expect_cat=true;cat_failure=MINI_OK;
    api=(mini_api_t){.struct_size=sizeof(api),.api_version=MINISHELL_API_VERSION,.system=&system_api,
        .memory=&memory_api,.fs=&fs,.audio=&audio,.serial=&serial_api,.display=&display_api,.input=&input_api,.time_location=&time_api};
}
static int live(void)
{ char *args[]={"ft8","--profile","adv","--cat","control:test","--rx","audio:test"};return ft8_lifecycle_entry(7,args); }
int main(void)
{
    reset();ready_at=UINT64_MAX;assert(live()==0);assert(opens>=10 && input_reads==120 && seen==31 && qso_actions==1);
    assert(!writes && !rx_starts && !worker_starts && !tx_steps && destroys==1);
    reset();ready_at=1200000;assert(live()==0);assert(opens==5 && writes==4 && rx_starts==1 && worker_starts==1 && worker_stops==1 && destroys==1);
    reset();assert(live()==0);assert(opens==1 && last_open==0 && writes==4 && rx_starts==1 && worker_starts==1 && worker_stops==1);
    const mini_result_t failures[]={MINI_ERR_IO,MINI_ERR_UNSUPPORTED,MINI_ERR_ACCESS};
    for(unsigned i=0;i<sizeof(failures)/sizeof(*failures);++i){reset();cat_failure=failures[i];assert(live()==12);assert(normal_frames==1 && opens==1 && !rx_starts && !input_reads && destroys==1);}
    reset();ready_at=1200000;cat_failure=MINI_ERR_IO;
    assert(live()==12 && opens==5 && qso_actions==1 && !writes && !rx_starts && destroys==1);
    reset();fail_rx=true;assert(live()==8 && writes==4 && rx_starts==1 && !worker_starts);
    reset();fail_worker=true;assert(live()==14 && rx_starts==1 && worker_starts==1 && !worker_stops);
    reset();fail_render=true;assert(live()==5 && !opens && !rx_starts);
    reset();expect_cat=false;fixture=true;char *fixture_args[]={"ft8","--profile","adv","--rx","/tmp/fixture.wav","--rx-slot","123"};
    assert(ft8_lifecycle_entry(7,fixture_args)==0 && rx_starts==1 && worker_starts==1 && !tx_steps && !opens);
    reset();expect_cat=false;char *file_args[]={"ft8","--profile","adv","--rx","/tmp/fixture.wav"};
    assert(ft8_lifecycle_entry(5,file_args)==0 && rx_starts==1 && worker_starts==1 && !opens);
    reset();expect_cat=false;char *offline_args[]={"ft8","--profile","adv"};
    assert(ft8_lifecycle_entry(3,offline_args)==0 && !rx_starts && !worker_starts && !opens);
    reset();char *tone_args[]={"ft8","--cat","control:test","--cat-test-tone","1500","--cat-test-ms","500"};
    assert(ft8_lifecycle_entry(7,tone_args)==0 && tone_tests==1 && !normal_frames && !destroys);
    puts("ft8_pending_start: PASS");return 0;
}

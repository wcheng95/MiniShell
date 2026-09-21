#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "config_service.h"
#include "ui_shell.h"

static char disk[1024], temp[1024];
static bool exists, opened, is_temp;
static size_t offset, length;
static unsigned fault, order;
static mini_result_t open_file(const char *path, uint32_t flags, mini_file_t *out)
{
    assert(!opened); offset=0;
    is_temp = strcmp(path, KEYER_SETTING_PATH) != 0;
    if (is_temp) {
        assert(!strcmp(path,"/flash/keyer/setting.tmp"));
        assert(flags==(MINI_FS_WRITE|MINI_FS_CREATE|MINI_FS_TRUNC));
        if(fault==1) return MINI_ERR_IO;
        temp[0]=0; length=0; order=1;
    } else { if(!exists)return MINI_ERR_NOT_FOUND; length=strlen(disk); }
    *out=1; opened=true; return MINI_OK;
}
static mini_result_t read_file(mini_file_t h, void *b, uint32_t n, uint32_t *got)
{
    assert(h==1 && opened && !is_temp);
    if(n>length-offset)n=(uint32_t)(length-offset);
    if(n>13)n=13;
    memcpy(b,disk+offset,n); offset+=n; *got=n; return MINI_OK;
}
static mini_result_t write_file(mini_file_t h,const void *b,uint32_t n,uint32_t *wrote)
{
    assert(h==1 && opened && is_temp && order==1);
    *wrote=0;
    if(fault==2) return MINI_ERR_IO;
    if(fault==6) return MINI_OK;
    if(n>7)n=7;
    assert(offset+n<sizeof(temp)); memcpy(temp+offset,b,n); offset+=n; temp[offset]=0; *wrote=n;
    return MINI_OK;
}
static mini_result_t sync_file(mini_file_t h)
{ assert(h==1 && opened && order==1); order=2; return fault==3?MINI_ERR_IO:MINI_OK; }
static mini_result_t close_file(mini_file_t h)
{
    assert(h==1 && opened); opened=false;
    if(is_temp)order=3;
    return fault==4?MINI_ERR_IO:MINI_OK;
}
static mini_result_t rename_file(const char *a,const char *b)
{
    assert(!opened && order==3); order=4;
    assert(!strcmp(a,"/flash/keyer/setting.tmp") && !strcmp(b,KEYER_SETTING_PATH));
    if(fault==5)return MINI_ERR_IO;
    strcpy(disk,temp); exists=true; return MINI_OK;
}
static mini_result_t remove_file(const char *p)
{ assert(!strcmp(p,"/flash/keyer/setting.tmp")); temp[0]=0; return MINI_OK; }
static mini_result_t mkdir_file(const char *p)
{ assert(!strcmp(p,"/flash/keyer")); return MINI_ERR_EXISTS; }
static const mini_fs_api_t fs={.open=open_file,.read=read_file,.write=write_file,.sync=sync_file,
    .close=close_file,.rename=rename_file,.remove_file=remove_file,.mkdir=mkdir_file};
static const mini_api_t api={.fs=&fs};
static const char canonical[]=
"wpm=20\nvolume=80\nsidetone=On\nsidetone_hz=700\nkey_in=Paddle\nkey_out=SKS\npaddle=IambicA\n"
"key_in_tip_gpio=13\nkey_in_ring_gpio=15\nkey_out_tip_gpio=3\nkey_out_ring_gpio=6\n"
"m1=CQ POTA\nm2=\nm3=\nm4=\nm5=\nrepeat_s=10\ntx_delay_s=1\ntune_timeout_s=10\nmute=Off\n";
int main(void)
{
    keyer_config_t c,loaded; bool from;
    assert(config_service_load(&api,&c,&from)==MINI_OK && !from);
    assert(config_service_save(&api,&c)==MINI_OK);
    assert(!strcmp(disk,canonical));
    c.wpm=60;c.volume=1;c.sidetone_enabled=false;c.sidetone_hz=999;c.key_in_mode=KEYER_KEY_IN_SK_R;
    c.key_out_mode=KEYER_KEY_OUT_SKM;c.paddle_mode=KEYER_ENGINE_PADDLE_BUG;
    c.key_in_tip_line=1;c.key_in_ring_line=2;c.key_out_tip_line=4;c.key_out_ring_line=5;
    c.repeat_s=99;c.tx_delay_s=99;c.tune_timeout_s=20;c.mute=true;
    for(unsigned i=0;i<5;++i){memset(c.messages[i],'A'+i,95);c.messages[i][95]=0;}
    c.messages[0][0]=' ';c.messages[0][2]='=';c.messages[0][94]=' ';
    assert(config_service_save(&api,&c)==MINI_OK);
    char expected[1024];strcpy(expected,disk);
    assert(config_service_load(&api,&loaded,&from)==MINI_OK && from);
    assert(loaded.messages[0][0]==' ' && loaded.messages[0][94]==' ');
    assert(config_service_save(&api,&loaded)==MINI_OK && !strcmp(expected,disk));
    for(fault=1;fault<=6;++fault) {
        c.volume=50;
        assert(config_service_save(&api,&c)!=MINI_OK && !opened);
        assert(!strcmp(disk,expected));
    }
    fault=0;
    const char *invalid[]={"wpm=4","volume=100","sidetone_hz=1000","repeat_s=0","repeat_s=100",
        "tx_delay_s=100","tune_timeout_s=21","mute=yes","key_out=Paddle","key_out=PaddleR"};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);++i){strcpy(disk,invalid[i]);assert(config_service_load(&api,&loaded,NULL)==MINI_ERR_INVALID);}
    strcpy(disk,"m1=");memset(disk+3,'E',96);disk[99]=0;
    assert(config_service_load(&api,&loaded,NULL)==MINI_ERR_INVALID);
    strcpy(disk,"key_out=SK\n");assert(config_service_load(&api,&loaded,NULL)==MINI_OK && loaded.key_out_mode==KEYER_KEY_OUT_SKS);
    strcpy(disk,"key_out=SK-M\n");assert(config_service_load(&api,&loaded,NULL)==MINI_OK && loaded.key_out_mode==KEYER_KEY_OUT_SKM);
    strcpy(disk,canonical);assert(config_service_load(&api,&c,NULL)==MINI_OK);
    ui_shell_t ui;ui_shell_init(&ui);
    ui_result_t r=ui_shell_input(&ui,&c,(ui_input_t){UI_CHAR,']',0});assert(r.action==UI_ACT_SAVE);
    assert(config_service_save(&api,&c)==MINI_OK);
    assert(config_service_load(&api,&loaded,NULL)==MINI_OK && loaded.wpm==21);
    assert(loaded.key_in_tip_line==13 && loaded.key_in_ring_line==15 && loaded.key_out_tip_line==3 && loaded.key_out_ring_line==6);
    /* Runtime-applied policy is explicit: failure retains the new value. */
    r=ui_shell_input(&ui,&c,(ui_input_t){UI_CHAR,'}',0});assert(r.action==UI_ACT_SAVE && c.volume==85);
    fault=5;assert(config_service_save(&api,&c)==MINI_ERR_IO && c.volume==85);fault=0;
    assert(config_service_load(&api,&loaded,NULL)==MINI_OK && loaded.volume==80);
    puts("keyer K6 config: PASS");
}

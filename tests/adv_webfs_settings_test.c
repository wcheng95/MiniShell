#include "adv_webfs_settings.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static webfs_credentials_t credentials;
static char input[WEBFS_SETTINGS_CAP + 2];
static size_t input_size, position, read_limit;
static unsigned opens, closes, reads, fail_read;
static mini_result_t open_result, close_result;
static bool invalid_count;

static void empty(void)
{
    const unsigned char *p = (const unsigned char *)&credentials;
    for (size_t i=0;i<sizeof(credentials);++i) assert(!p[i]);
}
static bool parse(const char *text)
{
    memset(&credentials,0x55,sizeof(credentials));
    bool valid=webfs_settings_parse(text,strlen(text),&credentials);
    if(!valid) empty();
    return valid;
}
static mini_result_t open_file(const char *path,uint32_t flags,mini_file_t *file)
{
    assert(!strcmp(path,"/flash/minishell/setting.txt") && flags==MINI_FS_READ);
    ++opens;if(open_result==MINI_OK)*file=7;return open_result;
}
static mini_result_t read_file(mini_file_t file,void *data,uint32_t size,uint32_t *count)
{
    assert(file==7 && closes==0 && size>0 && size<=WEBFS_SETTINGS_CAP);
    if(++reads==fail_read)return MINI_ERR_IO;
    if(invalid_count){*count=size+1;return MINI_OK;}
    size_t n=input_size-position;
    if(n>size)n=size;
    if(n>read_limit)n=read_limit;
    memcpy(data,input+position,n);position+=n;*count=(uint32_t)n;return MINI_OK;
}
static mini_result_t close_file(mini_file_t file)
{ assert(file==7 && !closes);++closes;return close_result; }
static const mini_fs_api_t fs={.open=open_file,.read=read_file,.close=close_file};
static void reset(void)
{
    strcpy(input,"SSID=MiniShell\nPW=abc= 123 \n");input_size=strlen(input);
    position=opens=closes=reads=fail_read=0;read_limit=WEBFS_SETTINGS_CAP;
    open_result=close_result=MINI_OK;invalid_count=false;
    memset(&credentials,0x55,sizeof(credentials));
}
static void load(bool expected)
{
    assert(webfs_settings_load(&fs,&credentials)==expected);
    assert(opens==1 && closes==(open_result==MINI_OK ? 1u : 0u));
    if(!expected)empty();
}
static void parser_tests(void)
{
    assert(parse("SSID=A\nPW=12345678\n"));assert(!strcmp(credentials.ssid,"A"));
    assert(parse("PW=12345678\r\nSSID=A\r\n"));
    assert(parse("\n\r\n  #SSID=bad\r\n\t# PW=bad\nother=x\nssid=ignored\nPW=abc= 123 \nSSID= A=B \n"));
    assert(!strcmp(credentials.ssid," A=B "));assert(!strcmp(credentials.password,"abc= 123 "));
    assert(parse("SSID= \nPW=        ")); /* Literal spaces are printable values. */
    assert(parse("SSID=A\nPW=12345678")); /* No final newline. */
    assert(parse("SSID=A\nSSID =ignored\n SSID=ignored\nPW=12345678\npw=ignored"));
    const char *bad[]={"", "SSID=A", "PW=12345678", "SSID=\nPW=12345678", "SSID=A\nPW=1234567",
        "SSID=A\nPW=", "SSID=A\nSSID=A\nPW=12345678", "SSID=A\nPW=12345678\nPW=12345678",
        " SSID=A\nPW=12345678", "SSID =A\nPW=12345678", "SSID=A\n PW=12345678",
        "SSID=A\nPW =12345678", "ssid=A\npw=12345678"};
    for(size_t i=0;i<sizeof(bad)/sizeof(*bad);++i)assert(!parse(bad[i]));
    for(unsigned length=0;length<=33;++length){
        strcpy(input,"SSID=");memset(input+5,'s',length);strcpy(input+5+length,"\nPW=12345678");
        assert(parse(input)==(length>=1 && length<=32));
        if(length && length<=32)assert(strlen(credentials.ssid)==length);
    }
    for(unsigned length=0;length<=64;++length){
        strcpy(input,"SSID=A\nPW=");memset(input+10,'p',length);input[10+length]=0;
        assert(parse(input)==(length>=8 && length<=63));
        if(length>=8 && length<=63)assert(strlen(credentials.password)==length);
    }
    /* Explicit lengths ensure embedded NUL cannot hide invalid value bytes. */
    for(unsigned byte=0;byte<=255;++byte){
        if(byte>=32 && byte<=126)continue;
        if(byte=='\r'||byte=='\n')continue; /* Line delimiters, not value bytes. */
        strcpy(input,"SSID=A\nPW=12345678");input[5]=(char)byte;
        assert(!webfs_settings_parse(input,18,&credentials));empty();
        strcpy(input,"SSID=A\nPW=12345678");input[11]=(char)byte;
        assert(!webfs_settings_parse(input,18,&credentials));empty();
    }
    assert(!webfs_settings_parse(NULL,0,&credentials));empty();
}
static void loader_tests(void)
{
    reset();load(true);assert(!strcmp(credentials.password,"abc= 123 "));
    for(unsigned chunk=1;chunk<=32;++chunk){reset();read_limit=chunk;load(true);}
    reset();read_limit=3;load(true);unsigned calls=reads;
    for(unsigned i=1;i<=calls;++i){reset();read_limit=3;fail_read=i;load(false);}
    reset();open_result=MINI_ERR_NOT_FOUND;load(false);assert(!reads);
    reset();open_result=MINI_ERR_ACCESS;load(false);
    reset();close_result=MINI_ERR_IO;load(false);
    reset();invalid_count=true;load(false);
    reset();input_size=0;load(false);
    reset();strcpy(input,"SSID=A");input_size=strlen(input);load(false);
    for(unsigned size=WEBFS_SETTINGS_CAP-1;size<=WEBFS_SETTINGS_CAP+1;++size){
        reset();input[input_size++]='#';memset(input+input_size,'x',size-input_size);input_size=size;
        load(size<=WEBFS_SETTINGS_CAP);
    }
    reset();input[input_size++]='#';memset(input+input_size,'x',WEBFS_SETTINGS_CAP-input_size);input_size=WEBFS_SETTINGS_CAP;
    fail_read=2;load(false); /* Failure in the exact-edge EOF probe. */
    assert(!webfs_settings_parse(input,WEBFS_SETTINGS_CAP+1,&credentials));empty();
    /* Each launch loads anew; no cached pair survives a later missing/invalid file. */
    reset();load(true);reset();open_result=MINI_ERR_NOT_FOUND;load(false);
    reset();strcpy(input,"SSID=Next\nPW=next pass");input_size=strlen(input);load(true);
    assert(!strcmp(credentials.ssid,"Next") && !strcmp(credentials.password,"next pass"));
}
int main(void){parser_tests();loader_tests();puts("adv_webfs_settings: PASS");return 0;}

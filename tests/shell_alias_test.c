#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../core/shell.c"

static const char *data, *commands[8];
static unsigned command_index, opens, closes, reads, launches, diagnostics;
static size_t position;
static unsigned read_size=7;
static mini_result_t open_result, close_result;
static unsigned fail_read;
static bool absent_service;
static char output[4096], launched[256];
static mini_result_t fs_open(const char *path, uint32_t flags, mini_file_t *file)
{
    assert(strcmp(path,"/flash/minishell/alias.txt")==0 && flags==MINI_FS_READ);
    ++opens; position=0; *file=1; return open_result;
}
static mini_result_t fs_read(mini_file_t file, void *buffer, uint32_t capacity, uint32_t *count)
{
    assert(file==1); ++reads;
    if (fail_read==reads) return MINI_ERR_IO;
    size_t remaining=strlen(data)-position;
    if (remaining>read_size) remaining=read_size;
    if (remaining>capacity) remaining=capacity;
    memcpy(buffer,data+position,remaining); position+=remaining; *count=(uint32_t)remaining; return MINI_OK;
}
static mini_result_t fs_close(mini_file_t file) { assert(file==1); ++closes; return close_result; }
static const mini_fs_api_t fs={.struct_size=sizeof(fs),.open=fs_open,.read=fs_read,.close=fs_close};
static const mini_api_t api={.struct_size=sizeof(api),.fs=&fs};
const mini_api_t *mini_api_get(void) { return absent_service ? NULL : &api; }
void minishell_platform_console_write(const char *text)
{
    assert(strlen(output)+strlen(text)<sizeof(output)); strcat(output,text);
    if (strstr(text,"alias: cannot read")) ++diagnostics;
}
int minishell_platform_console_read_line(char *buffer, size_t capacity)
{
    const char *text=commands[command_index++];
    if (!text) return 0;
    assert(strlen(text)<capacity); strcpy(buffer,text); return (int)strlen(text);
}
const char *minishell_platform_name(void) { return "mock"; }
minishell_platform_result_t minishell_app_list(minishell_app_emit_fn emit, void *ctx)
{ emit("listed",ctx); return MINISHELL_PLATFORM_OK; }
minishell_platform_result_t minishell_app_run(const char *name, int argc, char **argv, int *result)
{
    assert(argc>0 && strcmp(name,argv[0])==0); ++launches;
    strcpy(launched,name); *result=0; return MINISHELL_PLATFORM_OK;
}
static void reset(void)
{
    data="x=changed\n"; memset(commands,0,sizeof(commands)); commands[0]="x"; commands[1]="exit";
    opens=closes=reads=launches=diagnostics=command_index=0; position=0;
    open_result=close_result=MINI_OK; fail_read=0; read_size=7; absent_service=false;
    output[0]=launched[0]=0;
}
static void parser(void)
{
    struct {const char *line, *name, *rhs;} cases[]={
        {"u=usbmsc","u","usbmsc"}, {"f=ft8 --cat serial:node","f","ft8 --cat serial:node"},
        {"x=app --arg=a=b=c","x","app --arg=a=b=c"}, {"x= a=b \r\n","x"," a=b "},
        {"x=help\r\n","x","help"}, {"","x",NULL}, {"\r\n","x",NULL},
        {" \t# x=help","x",NULL}, {"=help","",NULL}, {"a b=help","a b",NULL},
        {"a\tb=help","a\tb",NULL}, {"a\rb=help","a\rb",NULL},
        {"a\nb=help","a\nb",NULL}, {"x=\r\n","x",NULL}, {"x=","x",NULL},
        {"x","x",NULL}, {" x=help","x",NULL}, {"xy=help","x",NULL},
    };
    for (unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);++i) {
        char line[256]; strcpy(line,cases[i].line);
        const char *rhs=minishell_alias_match(line,cases[i].name,strlen(cases[i].name));
        if (cases[i].rhs) assert(rhs && strcmp(rhs,cases[i].rhs)==0); else assert(!rhs);
    }
    char out[SHELL_LINE_MAX], rhs[SHELL_LINE_MAX];
    assert(minishell_alias_expand("app --arg=a=b=c","extra words",out,sizeof(out)));
    assert(strcmp(out,"app --arg=a=b=c extra words")==0);
    memset(rhs,'a',sizeof(rhs)-1); rhs[sizeof(rhs)-1]=0;
    assert(minishell_alias_expand(rhs,"",out,sizeof(out)) && strlen(out)==255);
    strcpy(out,"unchanged"); assert(!minishell_alias_expand(rhs,"x",out,sizeof(out)));
    assert(strcmp(out,"unchanged")==0);
    rhs[253]=0; assert(minishell_alias_expand(rhs,"x",out,sizeof(out)) && strlen(out)==255);
    assert(!minishell_alias_expand(rhs,"xx",out,sizeof(out)));
    assert(!minishell_alias_expand("","",out,0));
}
static void lookup(void)
{
    char replacement[SHELL_LINE_MAX]; reset();
    data="# comment\r\nx=first\r\ninvalid\nx=\nx=last=a=b";
    for (read_size=1;read_size<=128;read_size*=2) {
        assert(lookup_alias("x",1,replacement)==1 && strcmp(replacement,"last=a=b")==0);
    }
    assert(opens==closes);
    char lines[1024]; memset(lines,'a',sizeof(lines));
    memcpy(lines,"x=",2); strcpy(lines+800,"\nx=after-long\n"); data=lines;
    assert(lookup_alias("x",1,replacement)==1 && strcmp(replacement,"after-long")==0);
    data="x=reloaded\n";
    assert(lookup_alias("x",1,replacement)==1 && strcmp(replacement,"reloaded")==0);
}
static void failures(void)
{
    for (unsigned which=0;which<6;++which) {
        reset();
        if (which==0) open_result=MINI_ERR_NOT_FOUND;
        if (which==1) absent_service=true;
        if (which==2) open_result=MINI_ERR_UNSUPPORTED;
        if (which==3) open_result=MINI_ERR_ACCESS;
        if (which==4) { read_size=128; fail_read=2; } /* reject even a found match */
        if (which==5) close_result=MINI_ERR_IO;
        assert(minishell_shell_run()==0);
        assert(launches==1 && strcmp(launched,"x")==0);
        assert(diagnostics==(which>=3 ? 1u : 0u));
        assert(closes==(which>=4 ? 1u : 0u));
    }
    reset(); open_result=MINI_ERR_IO;
    commands[0]="help"; commands[1]="apps"; commands[2]="status"; commands[3]="run x"; commands[4]="exit";
    assert(minishell_shell_run()==0 && opens==0 && diagnostics==0);
    assert(launches==1 && strcmp(launched,"x")==0);
}
int main(void)
{
    parser(); lookup(); failures();
    puts("shell aliases: parser, boundaries, streaming, reload, FS failure fallback PASS");
    return 0;
}

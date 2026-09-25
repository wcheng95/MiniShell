/* Real runtime, shell, settings and app-manager sequencing over fake platform IO. */
#include "minishell_runtime.h"
#include "platform_backend.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const char *setting_text, *alias_text, *interactive[4];
static const char *file_text;
static size_t offset;
static bool active_file, initialized, configured, started, app_active;
static unsigned setting_opens, prompts, input_index;
static unsigned begins, ends, wait_state;
static char output[8192], events[1024];
static void event(const char *text) { assert(strlen(events)+strlen(text)<sizeof(events)); strcat(events,text); }
static mini_result_t open_file(const char *path, uint32_t flags, mini_file_t *file)
{
    assert(started && configured && !active_file && flags==MINI_FS_READ);
    if (!strcmp(path,"/flash/minishell/setting.txt")) { file_text=setting_text; ++setting_opens; }
    else { assert(!strcmp(path,"/flash/minishell/alias.txt")); file_text=alias_text; }
    if (!file_text) return MINI_ERR_NOT_FOUND;
    offset=0; active_file=true; *file=7; return MINI_OK;
}
static mini_result_t read_file(mini_file_t file, void *data, uint32_t size, uint32_t *count)
{
    assert(file==7 && active_file);
    size_t n=strlen(file_text)-offset;
    if(n>size)n=size;
    if(n>13)n=13;
    memcpy(data,file_text+offset,n);offset+=n;*count=(uint32_t)n;return MINI_OK;
}
static mini_result_t close_file(mini_file_t file)
{ assert(file==7 && active_file);active_file=false;return MINI_OK; }
static const mini_fs_api_t fs={.open=open_file,.read=read_file,.close=close_file};
static const mini_api_t api={.struct_size=sizeof(api),.fs=&fs};
const mini_api_t *mini_api_get(void) { assert(configured);return &api; }
int minishell_platform_init(void) { assert(!initialized);initialized=true;return 0; }
void minishell_platform_shutdown(void) { assert(!started && !configured);initialized=false; }
int minishell_platform_resource_limits(minishell_resource_limits_t *out)
{ assert(initialized);memset(out,0,sizeof(*out));return 0; }
void minishell_services_set_resource_limits(const minishell_resource_limits_t *limits) { (void)limits; }
void minishell_platform_services_prepare(minishell_services_port_t *out) { memset(out,0,sizeof(*out)); }
void minishell_services_configure(const minishell_services_port_t *port) { configured=port!=NULL; }
void minishell_platform_services_started(void) { assert(configured);started=true; }
void minishell_platform_services_stopping(void) { assert(started && !active_file);started=false; }
void minishell_platform_console_write(const char *text)
{
    assert(strlen(output)+strlen(text)<sizeof(output));strcat(output,text);
    if(!strcmp(text,"M$> ")) { assert(!app_active && !active_file);++prompts;event("prompt;"); }
}
void minishell_platform_console_clear(void) {}
void minishell_platform_console_prompt(void) { minishell_platform_console_write("M$> "); }
int minishell_platform_console_read_line(shell_editor_t *editor)
{
    assert(prompts==input_index+1 && !app_active);
    const char *text=interactive[input_index++];
    if(!text)return 0;
    assert(strlen(text)<sizeof(editor->line));strcpy(editor->line,text);return 1;
}
/* CWD semantics are exercised by the real Filesystem unit/Linux tests. */
mini_result_t minishell_filesystem_cwd_set(const char *path)
{ (void)path; return MINI_ERR_UNSUPPORTED; }
mini_result_t minishell_filesystem_cwd_get(char *out, size_t capacity)
{ assert(capacity >= 2); strcpy(out, "/"); return MINI_OK; }
const char *minishell_platform_name(void) { return "test"; }
void minishell_services_app_begin(void) { assert(!app_active && !active_file);app_active=true;++begins; }
void minishell_services_app_end(void) { assert(app_active);app_active=false;++ends; }
minishell_platform_result_t minishell_platform_apps_list(minishell_app_emit_fn emit,void *ctx)
{ emit("listed",ctx);return MINISHELL_PLATFORM_OK; }
minishell_platform_result_t minishell_platform_app_run(const char *name,int argc,char **argv,int *result)
{
    assert(app_active && argc && !strcmp(name,argv[0]));event(name);event(";");
    if(!strcmp(name,"wait")) {
        assert(!prompts && !wait_state);wait_state=1;event("app-return;");wait_state=2;
        /* Alias changes during an app are seen by the next startup command. */
        alias_text="b=after\nx=wait\n";
    }
    if(!strcmp(name,"after")) assert(wait_state==2 && !prompts && begins==ends+1);
    if(!strcmp(name,"missing"))return MINISHELL_PLATFORM_ERR_NOT_FOUND;
    if(!strcmp(name,"broken"))return MINISHELL_PLATFORM_ERR_LOAD;
    *result=!strcmp(name,"nonzero")?9:0;
    return MINISHELL_PLATFORM_OK;
}
static void reset(const char *settings)
{
    assert(!initialized && !active_file && !app_active);
    setting_text=settings;alias_text=NULL;
    memset(interactive,0,sizeof(interactive));interactive[0]="exit";
    setting_opens=prompts=input_index=begins=ends=wait_state=0;
    output[0]=events[0]=0;
}
static void run(void)
{ assert(minishell_run()==0 && setting_opens==1 && begins==ends && !initialized); }
int main(void)
{
    reset(NULL);run();assert(!begins && prompts==1);
    reset("startup=\nbrightness=invalid");run();assert(!begins);
    reset("startup=first");run();assert(!strcmp(events,"first;prompt;"));
    reset("brightness=50\nstartup= ;x;;b;missing;broken;nonzero;exit;last; \t;\n");
    alias_text="x=wait\nb=wrong\n";
    interactive[0]="interactive";interactive[1]="exit";run();
    assert(!strcmp(events,"wait;app-return;after;missing;broken;nonzero;last;prompt;interactive;prompt;"));
    assert(strstr(output,"missing: command not found\n"));
    assert(strstr(output,"app: broken launch failed (-3)\n"));
    assert(strstr(output,"app: nonzero returned 9\n"));
    assert(prompts==2 && !strstr(output,"wrong"));
    reset("startup=help;status;apps;run first;run;quit;last");alias_text="help=wrong\nquit=exit\n";run();
    assert(strstr(output,"show this help") && strstr(output,"platform : test") && strstr(output,"listed\n"));
    assert(strstr(output,"usage: run <app> [args...]"));
    assert(!strcmp(events,"first;last;prompt;"));
    char aliases[256];memcpy(aliases,"z=",2);memset(aliases+2,'a',252);aliases[254]='\n';aliases[255]=0;
    reset("startup=z arg;last");alias_text=aliases;run();
    assert(strstr(output,"alias: expansion too long\n") && !strcmp(events,"last;prompt;"));
    char long_setting[320];memcpy(long_setting,"startup=",8);memset(long_setting+8,'a',256);strcpy(long_setting+264,";last");
    reset(long_setting);run();assert(strstr(output,"shell: command too long\n") && !strcmp(events,"last;prompt;"));
    reset("startup=first");interactive[0]="left;right";interactive[1]="exit";run();
    assert(!strcmp(events,"first;prompt;left;right;prompt;"));assert(begins==2); /* No interactive semicolon parsing. */
    puts("resident boot dispatch and lifecycle: PASS");return 0;
}

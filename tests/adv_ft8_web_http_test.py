#!/usr/bin/env python3
"""Run production mirror lifecycle/HTTP code with deterministic SDK failure injection."""
from pathlib import Path
import subprocess
import sys
import tempfile
root=Path(sys.argv[1]).resolve()
src=(root/'platform/adv/adv_ft8_web.c').read_text()
for forbidden in ('/api/file','/api/list','/api/dir','/api/rename','HTTP_POST','HTTP_OPTIONS','Access-Control-Allow','webfs_http_start','websocket'):
    assert forbidden not in src
assert src.count('.uri =')==3
assert 'config.max_open_sockets = 2;' in src and 'config.task_priority = tskIDLE_PRIORITY + 1;' in src
assert 'config.stack_size = MIRROR_HTTP_STACK;' in src and '#define MIRROR_HTTP_STACK 6144u' in src
apps=(root/'platform/adv/adv_apps.c').read_text()
assert 'if (context->entry == minishell_app_ft8_main)' in apps
assert 'adv_ft8_web_run(context->argc, context->argv, context->entry)' in apps
assert 'else context->result = context->entry(context->argc, context->argv);' in apps
src='\n'.join(line for line in src.splitlines() if not line.startswith('#include'))
harness=r'''
#include "adv_ft8_web.h"
#include "adv_ft8_web_io.h"
#include "adv_ft8_web_page.h"
#include "adv_webfs_wifi.h"
#include <assert.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#define ESP_ERR_NO_MEM 2
#define ESP_FAIL -1
#define HTTPD_SOCK_ERR_FAIL -1
#define HTTPD_SOCK_ERR_TIMEOUT -2
#define HTTPD_400_BAD_REQUEST 400
#define HTTPD_RESP_USE_STRLEN -1
#define HTTP_GET 1
#define HTTP_PUT 2
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_8BIT 2
#define tskIDLE_PRIORITY 0
#define pdMS_TO_TICKS(x) (x)
static unsigned delay_calls;
static void vTaskDelay(unsigned ticks){assert(ticks==100);++delay_calls;}
static unsigned heap_caps_get_free_size(unsigned caps){assert(caps==3);return 100;}
static unsigned heap_caps_get_largest_free_block(unsigned caps){assert(caps==3);return 90;}
static unsigned heap_caps_get_minimum_free_size(unsigned caps){assert(caps==3);return 80;}
static unsigned uxTaskGetStackHighWaterMark(void *task){assert(!task);return 4096;}
static void log_line(const char *tag,const char *format,...){(void)tag;assert(!strstr(format,"password"));}
#define ESP_LOGI(...) log_line(__VA_ARGS__)
#define ESP_LOGE(...) log_line(__VA_ARGS__)
#define ESP_LOGW(...) log_line(__VA_ARGS__)
static const char *esp_err_to_name(int rc){(void)rc;return "fault";}
typedef void *httpd_handle_t;
typedef struct {void *user_ctx;const char *uri;size_t content_len;int method;} httpd_req_t;
typedef struct {const char *uri;int method;esp_err_t (*handler)(httpd_req_t *);void *user_ctx;} httpd_uri_t;
typedef struct {unsigned stack_size,task_priority,max_open_sockets,max_uri_handlers,recv_wait_timeout,send_wait_timeout;bool lru_purge_enable;esp_err_t (*open_fn)(httpd_handle_t,int);void *global_user_ctx;void (*global_user_ctx_free_fn)(void*);} httpd_config_t;
#define HTTPD_DEFAULT_CONFIG() ((httpd_config_t){0})
static void *server_ctx;
static unsigned stage,fail_stage,registrations,stops,entries,loads,keys;
static bool active_http,active_wifi,active_remote,configured=true;
static unsigned stop_failures;
static mini_result_t push_result;
static const char *status;
static unsigned char response[8192];static size_t response_length;
static char order[64];static size_t order_size;
static void mark(char c){order[order_size++]=c;order[order_size]=0;}
static bool fail(void){return ++stage==fail_stage;}
static const mini_api_t api={0};
const mini_api_t *mini_api_get(void){return &api;}
bool webfs_settings_load(const mini_fs_api_t *fs,webfs_credentials_t *out){assert(fs==api.fs);++loads;memset(out,0,sizeof(*out));strcpy(out->ssid,"Stable");strcpy(out->password,"password");return configured;}
esp_err_t webfs_wifi_start(webfs_wifi_t *wifi,const webfs_credentials_t *credentials){(void)wifi;assert((credentials!=NULL)==configured);mark('W');active_wifi=true;return fail()?ESP_FAIL:ESP_OK;}
void webfs_wifi_stop(webfs_wifi_t *wifi){(void)wifi;assert(!active_http && !active_remote);mark('w');active_wifi=false;}
bool adv_remote_input_start(void){mark('R');if(fail())return false;active_remote=true;return true;}
void adv_remote_input_stop(void){assert(!active_http);mark('r');active_remote=false;}
mini_result_t adv_remote_input_push(const mini_key_event_t *event){assert(event->struct_size==sizeof(*event));++keys;return push_result;}
void adv_display_snapshot(adv_display_snapshot_t *out){out->generation=0x12345678;memset(out->cells,'X',140);memset(out->attrs,1,140);}
static esp_err_t httpd_start(httpd_handle_t *server,const httpd_config_t *config){assert(config->stack_size==6144 && config->max_open_sockets==2 && config->task_priority==1);mark('H');if(fail())return ESP_FAIL;*server=(void*)1;server_ctx=config->global_user_ctx;active_http=true;return ESP_OK;}
static esp_err_t httpd_register_uri_handler(httpd_handle_t server,const httpd_uri_t *uri){assert(server && uri->handler);++registrations;assert(!strcmp(uri->uri,registrations==1?"/":registrations==2?"/api/screen":"/api/key"));assert(uri->method==(registrations==3?HTTP_PUT:HTTP_GET));return fail()?ESP_FAIL:ESP_OK;}
static esp_err_t httpd_stop(httpd_handle_t server){assert(server);++stops;if(stop_failures){--stop_failures;return ESP_FAIL;}mark('h');active_http=false;return ESP_OK;}
static void *httpd_get_global_user_ctx(httpd_handle_t server){(void)server;return server_ctx;}
static int recv(int fd,char *data,size_t size,int flags){(void)fd;(void)data;(void)size;(void)flags;errno=EAGAIN;return -1;}
static int send(int fd,const char *data,size_t size,int flags){(void)fd;(void)data;(void)size;(void)flags;errno=EAGAIN;return -1;}
static esp_err_t httpd_sess_set_recv_override(httpd_handle_t server,int fd,int (*fn)(httpd_handle_t,int,char*,size_t,int)){(void)server;(void)fd;assert(fn);return ESP_OK;}
static esp_err_t httpd_sess_set_send_override(httpd_handle_t server,int fd,int (*fn)(httpd_handle_t,int,const char*,size_t,int)){(void)server;(void)fd;assert(fn);return ESP_OK;}
static void httpd_resp_set_hdr(httpd_req_t *req,const char *key,const char *value){(void)req;assert(key && value);}
static void httpd_resp_set_type(httpd_req_t *req,const char *type){(void)req;assert(type);}
static void httpd_resp_set_status(httpd_req_t *req,const char *value){(void)req;status=value;}
static esp_err_t httpd_resp_send(httpd_req_t *req,const char *data,int length){(void)req;response_length=length<0?strlen(data):(size_t)length;assert(response_length<sizeof(response));memcpy(response,data,response_length);return ESP_OK;}
static esp_err_t httpd_resp_send_err(httpd_req_t *req,int code,const char *message){assert(code==400);status="400";return httpd_resp_send(req,message,-1);}
static size_t httpd_req_get_url_query_len(httpd_req_t *req){const char *q=strchr(req->uri,'?');return q?strlen(q+1):0;}
static esp_err_t httpd_req_get_url_query_str(httpd_req_t *req,char *out,size_t size){const char *q=strchr(req->uri,'?');if(!q||strlen(q+1)>=size)return ESP_FAIL;strcpy(out,q+1);return ESP_OK;}
'''
tests=r'''
static int entry(int argc,char **argv){assert(argc==1 && argv==NULL);++entries;mark('E');if(!fail_stage)assert(active_http && active_remote && active_wifi);else assert(!active_http && !active_remote && !active_wifi);return 17;}
static void reset(void){assert(!active_http && !active_remote && !active_wifi);stage=fail_stage=registrations=stops=entries=loads=keys=delay_calls=order_size=0;order[0]=0;configured=true;stop_failures=0;push_result=MINI_OK;}
int main(void){
 reset();assert(adv_ft8_web_run(1,NULL,entry)==17);assert(entries==1 && loads==1 && stops==1);assert(!strcmp(order,"WRHEhrw"));unsigned stages=stage;
 for(unsigned i=1;i<=stages;++i){reset();fail_stage=i;assert(adv_ft8_web_run(1,NULL,entry)==17);assert(entries==1 && loads==1);assert(!active_wifi && !active_remote && !active_http);}
 reset();configured=false;stop_failures=2;assert(adv_ft8_web_run(1,NULL,entry)==17 && stops==3);
 reset();mirror_t mirror={0};atomic_init(&mirror.stopping,false);server_ctx=&mirror;
 httpd_req_t req={.user_ctx=&mirror,.uri="/api/screen",.method=HTTP_GET};
 assert(handle_request(&req)==ESP_OK && response_length==284);
 assert(response[0]==0x78 && response[1]==0x56 && response[2]==0x34 && response[3]==0x12);
 for(unsigned i=0;i<140;++i)assert(response[4+i]=='X' && response[144+i]==1);
 req.uri="/";assert(handle_request(&req)==ESP_OK && response_length==sizeof(adv_ft8_web_page)-1);
 req.method=HTTP_PUT;req.uri="/api/key?c=79&m=0";assert(handle_request(&req)==ESP_OK && keys==1);
 push_result=MINI_ERR_NO_SPACE;handle_request(&req);assert(!strncmp(status,"429",3));
 push_result=MINI_ERR_NOT_READY;handle_request(&req);assert(!strncmp(status,"503",3));
 unsigned before=keys;req.uri="/api/key?c=79&m=32";handle_request(&req);assert(!strncmp(status,"400",3) && keys==before);
 req.content_len=1;assert(handle_request(&req)==ESP_FAIL && keys==before);req.content_len=0;
 assert(socket_receive(NULL,0,NULL,0,0)==HTTPD_SOCK_ERR_TIMEOUT);
 assert(socket_send(NULL,0,NULL,0,0)==HTTPD_SOCK_ERR_TIMEOUT);
 atomic_store(&mirror.stopping,true);assert(handle_request(&req)==ESP_FAIL && keys==before);
 assert(socket_receive(NULL,0,NULL,0,0)==HTTPD_SOCK_ERR_FAIL && socket_send(NULL,0,NULL,0,0)==HTTPD_SOCK_ERR_FAIL);
 mini_key_event_t event;char query[80];
 for(unsigned c=0;c<=127;++c){snprintf(query,sizeof(query),"c=%u&m=31",c);assert(adv_mirror_parse_key(query,&event)==(c>=32 && c<=126));if(c>=32 && c<=126)assert(event.codepoint==c && event.modifiers==31 && !event.key);}
 for(unsigned k=0;k<=19;++k){bool supported=(k>=1 && k<=6)||k==8||k==9||k==12||k==13;snprintf(query,sizeof(query),"m=0&k=%u",k);assert(adv_mirror_parse_key(query,&event)==supported);if(supported)assert(event.key==k && !event.codepoint);}
 const char *bad[]={"","c=65","c=65&m=0&x=1","c=65&c=65&m=0","k=1&c=65&m=0","c=65&m=0&m=0","c=65&m=0&","c=65&m=-1","c=65&m=32","c=%36%35&m=0","c=99999999999999&m=0","x=65&m=0","c=+65&m=0","c=65&m=0#","c=&m=0"};
 for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);++i)assert(!adv_mirror_parse_key(bad[i],&event));
 memset(query,'0',sizeof(query));assert(!adv_mirror_parse_key(query,&event));assert(!adv_mirror_parse_key(NULL,&event));
 puts("adv_ft8_web_http: PASS");return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='ft8-web-http-') as tmp:
    directory=Path(tmp)
    (directory/'esp_err.h').write_text('typedef int esp_err_t;\n#define ESP_OK 0\n')
    (directory/'esp_netif.h').write_text('typedef struct esp_netif esp_netif_t;\n')
    c=directory/'test.c';exe=directory/'test';c.write_text(harness+src+tests)
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-Wpedantic','-Wno-overlength-strings','-I'+tmp,
                    '-I'+str(root/'include'),'-I'+str(root/'platform/adv'),str(c),str(root/'platform/adv/adv_ft8_web_logic.c'),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)

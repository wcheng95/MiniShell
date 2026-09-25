#!/usr/bin/env python3
"""Execute the production HTTP read/mutation adapter with a fake request/Filesystem."""
from pathlib import Path
import subprocess
import tempfile
import sys

root = Path(sys.argv[1]).resolve()
src = (root / 'platform/adv/adv_webfs_http.c').read_text()
assert 'config.max_uri_handlers = 9;' in src
assert 'config.max_open_sockets = 2;' in src
assert 'config.stack_size = WEBFS_HTTP_STACK;' in src
for route in ('{"/api/file", HTTP_PUT}', '{"/api/file", HTTP_DELETE}',
              '{"/api/dir", HTTP_PUT}', '{"/api/dir", HTTP_DELETE}', '{"/api/rename", HTTP_PUT}'):
    assert route in src, route
for forbidden in ('HTTP_POST', 'HTTP_OPTIONS', 'Access-Control-Allow', 'fopen(', 'unlink(', 'f_rename('):
    assert forbidden not in src
assert 'return esp_random();' in src
fragment = src[src.index('typedef struct {'):src.index('esp_err_t webfs_http_start')]
harness = r'''
#include "adv_webfs_logic.h"
#include "adv_webfs_page.h"
#include <assert.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define HTTPD_RESP_USE_STRLEN -1
#define HTTP_PUT 1
#define HTTP_DELETE 2
typedef struct { webfs_buffers_t buffers; atomic_bool stopping; } webfs_http_t;
typedef struct { void *user_ctx; const char *uri; size_t content_len; int method; } httpd_req_t;
static const char *status;
static const char *read_header;
static bool attachment;
static size_t file_size, received;
static unsigned closes, terminators;
static mini_result_t stat_file(const char *path, mini_fs_stat_t *out)
{ assert(!strcmp(path,"/sd/a"));out->type=MINI_FS_TYPE_FILE;out->size=file_size;return MINI_OK; }
static mini_result_t open_file(const char *path,uint32_t flags,mini_file_t *out)
{ assert(!strcmp(path,"/sd/a") && flags==MINI_FS_READ);*out=9;return MINI_OK; }
static mini_result_t read_file(mini_file_t file,void *out,uint32_t size,uint32_t *count)
{ assert(file==9);memset(out,'a',size);*count=size;return MINI_OK; }
static mini_result_t close_file(mini_file_t file)
{ assert(file==9);++closes;return MINI_OK; }
static esp_err_t httpd_resp_send_chunk(httpd_req_t *r,const char *s,size_t n)
{ (void)r; if(!n){assert(!s);++terminators;}else{for(size_t i=0;i<n;++i)assert(s[i]=='a');received+=n;}return ESP_OK; }
static esp_err_t httpd_req_get_hdr_value_str(httpd_req_t *r,const char *key,char *out,size_t cap)
{ (void)r;assert(!strcmp(key,"X-WebFS-Read"));if(!read_header||strlen(read_header)>=cap)return ESP_FAIL;strcpy(out,read_header);return ESP_OK; }
static char response[256];
static mini_result_t result;
static unsigned operations;
static const char *last_path;
static int body_result;
static webfs_http_t http;
static mini_result_t mutate(const char *path) { ++operations;last_path=path;return result; }
static mini_result_t rename_file(const char *from, const char *to)
{ assert(!strcmp(from,"/sd/a"));assert(!strcmp(to,"/sd/b"));return mutate(to); }
static const mini_fs_api_t fs={.stat=stat_file,.open=open_file,.read=read_file,.close=close_file,.mkdir=mutate,.rmdir=mutate,.remove_file=mutate,.rename=rename_file};
static const mini_api_t api={.fs=&fs};
const mini_api_t *mini_api_get(void) { return &api; }
static void httpd_resp_set_status(httpd_req_t *r,const char *s) { (void)r;status=s; }
static void httpd_resp_set_type(httpd_req_t *r,const char *s) { (void)r;(void)s; }
static void httpd_resp_set_hdr(httpd_req_t *r,const char *k,const char *v) { (void)r;if(!strcmp(k,"Content-Disposition")){assert(!strcmp(v,"attachment"));attachment=true;} }
static esp_err_t httpd_resp_send(httpd_req_t *r,const char *s,int n)
{ (void)r;(void)n;snprintf(response,sizeof(response),"%s",s);return ESP_OK; }
static size_t httpd_req_get_url_query_len(httpd_req_t *r)
{ const char *q=strchr(r->uri,'?');return q?strlen(q+1):0; }
static esp_err_t httpd_req_get_url_query_str(httpd_req_t *r,char *out,size_t cap)
{ const char *q=strchr(r->uri,'?');if(!q||strlen(q+1)>=cap)return ESP_FAIL;strcpy(out,q+1);return ESP_OK; }
static int httpd_req_recv(httpd_req_t *r,void *data,size_t size)
{ (void)r;(void)data;(void)size;return body_result; }
static uint32_t esp_random(void) { return 42; }
'''
tests = r'''
int main(void)
{
    httpd_req_t req={.user_ctx=&http,.uri="/api/dir?path=/sd/a",.method=HTTP_PUT};
    const struct { mini_result_t rc; const char *status; } errors[]={
        {MINI_ERR_INVALID,"400"},{MINI_ERR_NAME_TOO_LONG,"400"},{MINI_ERR_NOT_FOUND,"404"},
        {MINI_ERR_ACCESS,"403"},{MINI_ERR_EXISTS,"409"},{MINI_ERR_NOT_EMPTY,"409"},
        {MINI_ERR_IS_DIR,"400"},{MINI_ERR_NOT_DIR,"400"},{MINI_ERR_NO_SPACE,"507"},
        {MINI_ERR_NOT_READY,"503"},{MINI_ERR_TOO_MANY_OPEN,"503"},{MINI_ERR_UNSUPPORTED,"501"},
        {MINI_ERR_IO,"500"}};
    for(size_t i=0;i<sizeof(errors)/sizeof(*errors);++i) {
        result=errors[i].rc;assert(handle_mutation(&req)==ESP_FAIL);
        assert(!strncmp(status,errors[i].status,3));assert(strstr(response,"error"));
    }
    result=MINI_OK;assert(handle_mutation(&req)==ESP_OK);assert(!strcmp(response,"{\"ok\":true}"));
    assert(!strcmp(last_path,"/sd/a"));
    req.method=HTTP_DELETE;assert(handle_mutation(&req)==ESP_OK);
    req.uri="/api/file?path=/sd/a";assert(handle_mutation(&req)==ESP_OK);
    req.method=HTTP_PUT;req.uri="/api/rename?from=/sd/a&to=/sd/b";assert(handle_mutation(&req)==ESP_OK);
    unsigned before=operations;
    req.uri="/api/rename?from=/sd/a&to=/flash/b";assert(handle_mutation(&req)==ESP_FAIL);
    req.uri="/api/rename?from=/sd/a&to=/sd/d/b";assert(handle_mutation(&req)==ESP_FAIL);
    req.uri="/api/rename?from=/sd/a&to=/sd/b&to=/sd/c";assert(handle_mutation(&req)==ESP_FAIL);
    req.uri="/api/dir?path=/sd";assert(handle_mutation(&req)==ESP_FAIL);
    req.uri="/api/dir?path=/sd/a";req.content_len=1;assert(handle_mutation(&req)==ESP_FAIL);
    assert(operations==before);
    uint32_t n=999;char data[8];
    body_result=-3;assert(receive_body(&req,data,sizeof(data),&n)==MINI_ERR_IO && !n);
    body_result=0;assert(receive_body(&req,data,sizeof(data),&n)==MINI_ERR_IO);
    body_result=3;assert(receive_body(&req,data,sizeof(data),&n)==MINI_OK && n==3);
    assert(receive_body(&req,NULL,0,&n)==MINI_OK && !n);
    atomic_store(&http.stopping,true);
    assert(receive_body(&req,data,sizeof(data),&n)==MINI_ERR_IO);
    assert(receive_body(&req,NULL,0,&n)==MINI_ERR_IO);
    assert(handle_mutation(&req)==ESP_FAIL && operations==before);
    assert(temp_random(NULL)==42);
    atomic_store(&http.stopping,false);
    req.content_len=0;req.uri="/api/file?path=/sd/a";
    const char *headers[]={NULL,"1","0","11"};
    for(unsigned h=0;h<4;++h) for(unsigned empty=0;empty<2;++empty) {
        read_header=headers[h];attachment=false;received=closes=terminators=0;
        file_size=empty?0:5000;
        assert(handle_request(&req)==ESP_OK);
        assert(attachment==(h!=1));assert(received==file_size);
        assert(closes==1 && terminators==1);
    }
    puts("adv_webfs_http: PASS");return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='webfs-http-') as tmp:
    c = Path(tmp) / 'test.c'
    exe = Path(tmp) / 'test'
    c.write_text(harness + fragment + tests)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-Wpedantic',
                    # Embedded HTML exceeds ISO C's minimum supported string length.
                    '-Wno-overlength-strings',
                    '-I'+str(root/'include'), '-I'+str(root/'platform/adv'), str(c),
                    str(root/'platform/adv/adv_webfs_logic.c'), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)

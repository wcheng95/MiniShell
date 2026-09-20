#include "adv_webfs_logic.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static webfs_buffers_t buffers;
static unsigned ops, fail_at, sinks, sink_fail, opened, closed, entries, position;
static uint64_t file_size, received;
static bool directory, file_active, dir_active, capture;
static char output[8192];
static size_t output_size, max_chunk;

static mini_result_t operation(void) { return ++ops == fail_at ? MINI_ERR_IO : MINI_OK; }
static mini_result_t space(const char *path, mini_fs_space_t *out)
{
    assert(webfs_valid_path(path));
    mini_result_t rc = operation();
    out->total_bytes=100000; out->used_bytes=123; out->free_bytes=99877;
    return rc;
}
static mini_result_t stat_path(const char *path, mini_fs_stat_t *out)
{
    assert(webfs_valid_path(path));
    mini_result_t rc = operation();
    out->type = directory ? MINI_FS_TYPE_DIRECTORY : MINI_FS_TYPE_FILE;
    out->size = file_size;
    return rc;
}
static mini_result_t dir_open(const char *path, mini_dir_t *out)
{
    assert(webfs_valid_path(path)); assert(!dir_active);
    mini_result_t rc = operation();
    if (rc == MINI_OK) { dir_active=true; ++opened; *out=7; }
    return rc;
}
static mini_result_t dir_read(mini_dir_t dir, mini_fs_dir_entry_t *out, uint32_t *has)
{
    assert(dir==7 && dir_active);
    mini_result_t rc = operation();
    *has=position<entries;
    if (*has) {
        out->type=position%2 ? MINI_FS_TYPE_FILE : MINI_FS_TYPE_DIRECTORY;
        if (!position) strcpy(out->name,"caf\xc3\xa9 & <entry>");
        else snprintf(out->name,sizeof(out->name),"file-%u", position);
        ++position;
    }
    return rc;
}
static mini_result_t dir_close(mini_dir_t dir)
{
    assert(dir==7 && dir_active); dir_active=false; ++closed;
    return operation();
}
static mini_result_t file_open(const char *path, uint32_t flags, mini_file_t *out)
{
    assert(webfs_valid_path(path)); assert(flags==MINI_FS_READ && !file_active);
    mini_result_t rc=operation();
    if (rc==MINI_OK) { file_active=true; ++opened; *out=9; }
    return rc;
}
static mini_result_t file_read(mini_file_t file, void *out, uint32_t size, uint32_t *count)
{
    assert(file==9 && file_active && size<=WEBFS_TRANSFER_CAP);
    mini_result_t rc=operation();
    *count=size>7 ? 7 : size;
    for (unsigned i=0;i<*count;++i) ((unsigned char *)out)[i]=(unsigned char)(position++%251);
    return rc;
}
static mini_result_t file_close(mini_file_t file)
{
    assert(file==9 && file_active); file_active=false; ++closed;
    return operation();
}
static const mini_fs_api_t fs = {.space=space,.stat=stat_path,
    .dir_open=dir_open,.dir_read=dir_read,.dir_close=dir_close,
    .open=file_open,.read=file_read,.close=file_close};

static bool sink(void *ctx, const char *data, size_t length)
{
    assert(ctx==&buffers);
    assert(length<=WEBFS_TRANSFER_CAP);
    if (++sinks == sink_fail) return false;
    if (length>max_chunk) max_chunk=length;
    if (capture) {
        assert(output_size+length<sizeof(output));
        memcpy(output+output_size,data,length);output_size+=length;output[output_size]=0;
    }
    if (!directory) for (size_t i=0;i<length;++i)
        assert((unsigned char)data[i] == (received+i)%251);
    received+=length;
    return true;
}
static void reset(bool listing)
{
    assert(!dir_active && !file_active);
    ops=fail_at=sinks=sink_fail=opened=closed=position=0;
    received=output_size=max_chunk=0; output[0]=0; entries=3; file_size=21;
    directory=listing; capture=false;
    strcpy(buffers.path,"/flash");
}
static void paths(void)
{
    const char *good[]={"path=/flash","path=/sd","path=/flash/a/b", "path=%2Fsd%2Fa",
        "path=/flash/caf%C3%A9", "path=/sd/a+b", "path=/sd/a%2Bb", "path=/sd/%252e%252e",
        "path=/flash/a%26b%3Dc%23d"};
    const char *expected[]={"/flash","/sd","/flash/a/b","/sd/a","/flash/caf\xc3\xa9",
        "/sd/a b","/sd/a+b","/sd/%2e%2e","/flash/a&b=c#d"};
    for (size_t i=0;i<sizeof(good)/sizeof(*good);++i) {
        assert(webfs_query_path(good[i], buffers.path, sizeof(buffers.path)));
        assert(!strcmp(buffers.path,expected[i]));
    }
    const char *bad[]={"", "path=", "path=sd", "path=/", "path=/flashy", "path=/sda/a",
        "path=/flash/../sd", "path=/sd/.", "path=/sd/a/./b", "path=/sd/%2e%2e/a",
        "path=/sd/a%2f..%2f..%2fflash", "path=/sd/%", "path=/sd/%0", "path=/sd/%GG",
        "path=/sd/%00a", "path=/sd/%1f", "path=/sd/%7f", "path=/sd/\n", "path=/sd/\\..\\flash",
        "path=/sd/%5c..", "path=/sd//a", "path=/sd/", "path=/sd&path=/flash", "path=/sd&x=1",
        "x=1&path=/sd", "Path=/sd", "path=/sd#a", "path=/sd=a"};
    for (size_t i=0;i<sizeof(bad)/sizeof(*bad);++i)
        assert(!webfs_query_path(bad[i], buffers.path, sizeof(buffers.path)));
    char small[4];
    assert(webfs_query_path("path=/sd",small,sizeof(small)));
    assert(!webfs_query_path("path=/sd",small,sizeof(small)-1));
    assert(!webfs_query_path(NULL,small,sizeof(small)));
    assert(!webfs_query_path("path=/sd",NULL,4));
    assert(!webfs_query_path("path=/sd",small,0));
    memset(buffers.query,'a',sizeof(buffers.query));
    assert(!webfs_query_path(buffers.query,buffers.path,sizeof(buffers.path)));
    strcpy(buffers.query,"path=/sd/");memset(buffers.query+9,'a',256);buffers.query[265]=0;
    assert(!webfs_query_path(buffers.query,buffers.path,sizeof(buffers.path)));
    buffers.query[264]=0;assert(webfs_query_path(buffers.query,buffers.path,sizeof(buffers.path)));
    /* Exact 511-byte decoded path, then a one-byte overflow, within query capacity. */
    strcpy(buffers.query,"path=/sd/");
    for (size_t i=9;i<516;++i) buffers.query[i]=(i%2 ? 'a' : '/');
    buffers.query[516]=0;
    assert(webfs_query_path(buffers.query,buffers.path,sizeof(buffers.path)));
    buffers.query[516]='a';buffers.query[517]=0;
    assert(!webfs_query_path(buffers.query,buffers.path,sizeof(buffers.path)));
}
static void json(void)
{
    char out[1600];
    assert(webfs_json_string("a\"\\\n\t\1\177 caf\xc3\xa9",out,sizeof(out)));
    assert(!strcmp(out,"\"a\\\"\\\\\\u000a\\u0009\\u0001\\u007f caf\xc3\xa9\""));
    assert(webfs_json_string("",out,3) && !strcmp(out,"\"\""));
    assert(!webfs_json_string("",out,2));
    assert(webfs_json_string("a",out,4));assert(!webfs_json_string("a",out,3));
    assert(!webfs_json_string(NULL,out,sizeof(out)));
    char worst[256];memset(worst,1,255);worst[255]=0;
    assert(webfs_json_string(worst,out,1533));assert(strlen(out)==1532);
    assert(!webfs_json_string(worst,out,1532));
}
static void streams(void)
{
    reset(true);capture=true;
    assert(webfs_list(&fs,&buffers,sink,&buffers)==MINI_OK);
    assert(!strcmp(output,"{\"total\":100000,\"used\":123,\"free\":99877,\"entries\":["
        "{\"name\":\"caf\xc3\xa9 & <entry>\" ,\"type\":\"dir\",\"size\":21},"
        "{\"name\":\"file-1\" ,\"type\":\"file\",\"size\":21},"
        "{\"name\":\"file-2\" ,\"type\":\"dir\",\"size\":21}]}"));
    unsigned calls=ops, sends=sinks;
    assert(opened==1 && closed==1);
    for (unsigned i=1;i<=calls;++i) {
        reset(true);fail_at=i;
        assert(webfs_list(&fs,&buffers,sink,&buffers)!=MINI_OK);
        assert(opened==closed);
    }
    for (unsigned i=1;i<=sends;++i) {
        reset(true);sink_fail=i;
        assert(webfs_list(&fs,&buffers,sink,&buffers)!=MINI_OK);
        assert(opened==closed);
    }
    reset(true);entries=0;capture=true;
    assert(webfs_list(&fs,&buffers,sink,&buffers)==MINI_OK && strstr(output,"\"entries\":[]}"));
    reset(true);entries=10000;
    assert(webfs_list(&fs,&buffers,sink,&buffers)==MINI_OK);
    assert(position==10000 && received>WEBFS_TRANSFER_CAP*100 && max_chunk<WEBFS_TRANSFER_CAP);
    reset(false);file_size=10001;
    assert(webfs_file(&fs,&buffers,sink,&buffers)==MINI_OK && received==10001);
    assert(opened==1 && closed==1);
    reset(false);assert(webfs_file(&fs,&buffers,sink,&buffers)==MINI_OK);
    calls=ops;sends=sinks;
    for (unsigned i=1;i<=calls;++i) {
        reset(false);fail_at=i;
        assert(webfs_file(&fs,&buffers,sink,&buffers)!=MINI_OK);assert(opened==closed);
    }
    for (unsigned i=1;i<=sends;++i) {
        reset(false);sink_fail=i;
        assert(webfs_file(&fs,&buffers,sink,&buffers)!=MINI_OK);assert(opened==closed);
    }
    reset(false);file_size=0;
    assert(webfs_file(&fs,&buffers,sink,&buffers)==MINI_OK && !received && closed==1);
    reset(true);assert(webfs_file(&fs,&buffers,sink,&buffers)==MINI_ERR_IS_DIR && !opened);
    reset(false);strcpy(buffers.path,"/sd/../flash");
    assert(webfs_file(&fs,&buffers,sink,&buffers)==MINI_ERR_INVALID && !ops);
    assert(webfs_list(&fs,&buffers,sink,&buffers)==MINI_ERR_INVALID && !ops);
}
int main(void)
{
    paths();json();streams();puts("adv_webfs: PASS");return 0;
}

#include "adv_webfs_logic.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static webfs_buffers_t b;
static unsigned calls, fail_at, opens, closes, commits, removes, randoms, collisions;
static unsigned receive_limit;
static unsigned probes, cancel_at, source_position, temp_size, destination_size;
static mini_result_t fault, cleanup_fault;
static bool active, temp_exists, destination_exists, destination_dir, synced;
static bool zero_write, oversize_write, zero_receive, oversize_receive;
static unsigned char temp[10000], destination[10000];
static char temp_path[WEBFS_PATH_CAP];
static const unsigned char old[] = "old destination remains intact";

static void intact(void)
{
    if (!commits && destination_exists && !destination_dir) {
        assert(destination_size == sizeof(old));
        assert(!memcmp(destination, old, sizeof(old)));
    }
}
static mini_result_t step(void) { intact(); return ++calls == fail_at ? fault : MINI_OK; }
static mini_result_t stat_path(const char *path, mini_fs_stat_t *out)
{
    assert(!strcmp(path,b.path));
    mini_result_t rc=step();
    if (rc!=MINI_OK) return rc;
    if (!destination_exists) return MINI_ERR_NOT_FOUND;
    out->type=destination_dir ? MINI_FS_TYPE_DIRECTORY : MINI_FS_TYPE_FILE;
    return MINI_OK;
}
static mini_result_t open_file(const char *path, uint32_t flags, mini_file_t *out)
{
    assert(flags==(MINI_FS_WRITE|MINI_FS_CREATE|MINI_FS_EXCL));
    assert(webfs_mutable_path(path) && !active && !temp_exists);
    assert(strcmp(path,b.path));
    assert(strstr(path,"/.webfs-upload-"));
    assert((strrchr(path,'/')-path)==(strrchr(b.path,'/')-b.path));
    assert(!strncmp(path,b.path,(size_t)(strrchr(path,'/')-path)));
    mini_result_t rc=step();
    if (rc!=MINI_OK) return rc;
    if (collisions) { --collisions; return MINI_ERR_EXISTS; }
    strcpy(temp_path,path); temp_exists=active=true; *out=17; ++opens;
    return MINI_OK;
}
static mini_result_t write_file(mini_file_t file, const void *data, uint32_t size, uint32_t *count)
{
    assert(file==17 && active && size<=WEBFS_TRANSFER_CAP && size);
    mini_result_t rc=step();
    if (rc!=MINI_OK) return rc;
    *count=zero_write?0:oversize_write?size+1:size>137?137:size;
    if (zero_write||oversize_write) return MINI_OK;
    assert(temp_size+*count<=sizeof(temp));
    memcpy(temp+temp_size,data,*count);temp_size+=*count;
    return MINI_OK;
}
static mini_result_t sync_file(mini_file_t file)
{
    assert(file==17 && active); mini_result_t rc=step();synced=rc==MINI_OK;return rc;
}
static mini_result_t close_file(mini_file_t file)
{
    assert(file==17 && active);active=false;++closes;return step();
}
static mini_result_t rename_file(const char *from, const char *to)
{
    assert(!active && synced && temp_exists && closes==1);
    assert(!strcmp(from,temp_path) && !strcmp(to,b.path));
    mini_result_t rc=step();
    if (rc==MINI_OK) {
        memcpy(destination,temp,temp_size);destination_size=temp_size;destination_exists=true;
        ++commits;temp_exists=false;
    }
    return rc;
}
static mini_result_t remove_temp(const char *path)
{
    intact();assert(!active && temp_exists && !strcmp(path,temp_path));++removes;
    if (cleanup_fault==MINI_OK) temp_exists=false;
    return cleanup_fault;
}
static mini_result_t receive(void *ctx, void *data, uint32_t size, uint32_t *count)
{
    assert(ctx==&b);intact();*count=0;
    if (++probes==cancel_at) return MINI_ERR_IO;
    if (!size) { assert(!data);return MINI_OK; }
    assert(size<=WEBFS_TRANSFER_CAP);
    *count=zero_receive?0:oversize_receive?size+1:(receive_limit && size>receive_limit)?receive_limit:size;
    if (zero_receive||oversize_receive) return MINI_OK;
    for (unsigned i=0;i<*count;++i) ((unsigned char *)data)[i]=(unsigned char)(source_position++%251);
    return MINI_OK;
}
static uint32_t random_name(void *ctx) { assert(ctx==&b);return ++randoms; }
static const mini_fs_api_t fs={.stat=stat_path,.open=open_file,.write=write_file,
    .sync=sync_file,.close=close_file,.rename=rename_file,.remove_file=remove_temp};
static void reset(bool exists)
{
    assert(!active);
    calls=fail_at=opens=closes=commits=removes=randoms=collisions=probes=cancel_at=source_position=temp_size=0;
    fault=MINI_ERR_IO;cleanup_fault=MINI_OK;receive_limit=0;
    temp_exists=destination_dir=synced=zero_write=oversize_write=zero_receive=oversize_receive=false;
    destination_exists=exists;destination_size=sizeof(old);memcpy(destination,old,sizeof(old));
    strcpy(b.path,"/flash/caf\xc3\xa9.bin");
}
static mini_result_t upload(size_t size) { return webfs_upload(&fs,&b,size,receive,random_name,&b); }
static void successful(size_t size)
{
    assert(upload(size)==MINI_OK);
    assert(opens==1 && closes==1 && commits==1 && !active && !temp_exists && !removes);
    assert(destination_size==size);
    for (unsigned i=0;i<size;++i) assert(destination[i]==i%251);
}
static void failed(mini_result_t expected)
{
    assert(upload(6001)==expected);intact();
    assert(!active && opens==closes && !commits);
    assert(removes==opens);
    assert(temp_exists==(opens && cleanup_fault!=MINI_OK));
}
static void uploads(void)
{
    reset(false);successful(6001);
    reset(true);successful(6001);unsigned n=calls,p=probes;
    reset(false);strcpy(b.path,"/sd/foo/bar");successful(6001);
    reset(true);receive_limit=317;successful(6001);
    reset(false);successful(0);reset(true);successful(0);
    for (unsigned i=1;i<=n;++i) {
        reset(true);fail_at=i;failed(MINI_ERR_IO);
        reset(false);fail_at=i;failed(MINI_ERR_IO);assert(!destination_exists);
        reset(true);fail_at=i;fault=MINI_ERR_NO_SPACE;failed(MINI_ERR_NO_SPACE);
        reset(true);fail_at=i;cleanup_fault=MINI_ERR_ACCESS;failed(MINI_ERR_IO);
    }
    /* Every receive/write cancellation boundary, plus before sync and after close. */
    for (unsigned i=1;i<=p;++i) { reset(true);cancel_at=i;failed(MINI_ERR_IO); }
    reset(true);collisions=2;successful(6001);assert(randoms==3);
    reset(true);collisions=100;failed(MINI_ERR_EXISTS);assert(randoms==WEBFS_TEMP_ATTEMPTS && !opens);
    reset(false);strcpy(b.path,"/flash/.WEBFS-UPLOAD-00000001.TMP");successful(1);assert(randoms==2);
    reset(true);destination_dir=true;failed(MINI_ERR_IS_DIR);assert(!randoms);
    reset(true);zero_write=true;failed(MINI_ERR_IO);
    reset(true);oversize_write=true;failed(MINI_ERR_IO);
    reset(true);zero_receive=true;failed(MINI_ERR_IO);
    reset(true);oversize_receive=true;failed(MINI_ERR_IO);
    reset(true);fail_at=1;fault=MINI_ERR_NOT_READY;failed(MINI_ERR_NOT_READY);
    /* 485-byte parent + 26-byte temp name is the exact 511-byte limit. */
    reset(false);strcpy(b.path,"/sd/");memset(b.path+4,'a',240);b.path[244]='/';
    memset(b.path+245,'b',239);strcpy(b.path+484,"/x");
    assert(webfs_valid_path(b.path));successful(1);assert(strlen(temp_path)==511);
    reset(true);strcpy(b.path,"/sd/");memset(b.path+4,'a',240);b.path[244]='/';
    memset(b.path+245,'b',240);strcpy(b.path+485,"/x");
    assert(webfs_valid_path(b.path));failed(MINI_ERR_NAME_TOO_LONG);assert(!randoms);
}

static unsigned mutations;
static mini_result_t mutation_result;
static mini_result_t mutation(const char *path)
{ assert(webfs_mutable_path(path));++mutations;return mutation_result; }
static mini_result_t rename_mutation(const char *from, const char *to)
{ assert(webfs_mutable_path(to));return mutation(from); }
static const mini_fs_api_t mutations_fs={.mkdir=mutation,.remove_file=mutation,.rmdir=mutation,.rename=rename_mutation};
static void namespace_tests(void)
{
    const char *bad[]={"/flash","/sd","/sd/","/sd/..","/sd/./x","/sd/a\\b","/other/a"};
    for (unsigned i=0;i<sizeof(bad)/sizeof(*bad);++i) {
        for (int op=WEBFS_MKDIR;op<=WEBFS_RENAME;++op)
            assert(webfs_mutate(&mutations_fs,(webfs_mutation_t)op,bad[i],"/sd/a")==MINI_ERR_INVALID);
        reset(true);strcpy(b.path,bad[i]);failed(MINI_ERR_INVALID);assert(!calls);
    }
    assert(!mutations);
    const mini_result_t results[]={MINI_OK,MINI_ERR_EXISTS,MINI_ERR_IO,MINI_ERR_IS_DIR,
        MINI_ERR_NOT_DIR,MINI_ERR_NOT_EMPTY,MINI_ERR_NOT_FOUND,MINI_ERR_NOT_READY,MINI_ERR_ACCESS};
    for (unsigned i=0;i<sizeof(results)/sizeof(*results);++i) {
        mutation_result=results[i];
        for (int op=WEBFS_MKDIR;op<=WEBFS_RENAME;++op)
            assert(webfs_mutate(&mutations_fs,(webfs_mutation_t)op,"/sd/caf\xc3\xa9","/sd/d")==results[i]);
    }
    mutation_result=MINI_OK;
    assert(webfs_mutate(&mutations_fs,WEBFS_RENAME,"/flash/a","/flash/a")==MINI_OK);
    unsigned before=mutations;
    const char *targets[]={"/sd/a","/flash/d/a","/flash","/flash/../a"};
    for (unsigned i=0;i<sizeof(targets)/sizeof(*targets);++i)
        assert(webfs_mutate(&mutations_fs,WEBFS_RENAME,"/flash/a",targets[i])==MINI_ERR_INVALID);
    assert(mutations==before);
    assert(webfs_query_rename("from=/sd/a&to=%2Fsd%2Fcaf%C3%A9",b.path,b.child,sizeof(b.path)));
    assert(!strcmp(b.path,"/sd/a") && !strcmp(b.child,"/sd/caf\xc3\xa9"));
    assert(webfs_query_rename("to=/flash/a%26b%3Dc&from=/flash/%252e",b.path,b.child,sizeof(b.path)));
    assert(!strcmp(b.path,"/flash/%2e") && !strcmp(b.child,"/flash/a&b=c"));
    const char *queries[]={"from=/sd/a", "from=/sd/a&to=/sd/b&to=/sd/c", "from=/sd/a&from=/sd/b",
        "to=/sd/a&to=/sd/b", "path=/sd/a&to=/sd/b", "to=/sd/b&x=/sd/a", "from=&to=/sd/a",
        "from=/sd/a&to=/sd/%", "from=/sd/a&to=/sd/%00", "from=/sd/..&to=/sd/a",
        "from=/sd/a&to=/sd/a=x", "from=/sd/a&to=/sd/a#b", "from=/sd/a&to=/sd/a&"};
    for (unsigned i=0;i<sizeof(queries)/sizeof(*queries);++i)
        assert(!webfs_query_rename(queries[i],b.path,b.child,sizeof(b.path)));
    memset(b.query,'a',sizeof(b.query));
    assert(!webfs_query_rename(b.query,b.path,b.child,sizeof(b.path)));
    assert(!webfs_query_rename(NULL,b.path,b.child,sizeof(b.path)));
}
/* MiniShell API fake with namespace state: check that each WebFS action reaches
 * the right API, and that API type/non-empty errors preserve the tree. */
static struct { const char *path; unsigned type; unsigned content; } nodes[8];
static int node(const char *path)
{
    for (unsigned i=0;i<8;++i) if (nodes[i].path && !strcmp(nodes[i].path,path)) return (int)i;
    return -1;
}
static mini_result_t tree_mkdir(const char *path)
{
    if (node(path)>=0) return MINI_ERR_EXISTS;
    for (unsigned i=0;i<8;++i) if (!nodes[i].path) {
        nodes[i].path=path;nodes[i].type=MINI_FS_TYPE_DIRECTORY;return MINI_OK;
    }
    return MINI_ERR_NO_SPACE;
}
static mini_result_t tree_remove(const char *path)
{
    int i=node(path);if(i<0)return MINI_ERR_NOT_FOUND;
    if(nodes[i].type!=MINI_FS_TYPE_FILE)return MINI_ERR_IS_DIR;
    nodes[i].path=NULL;return MINI_OK;
}
static mini_result_t tree_rmdir(const char *path)
{
    int i=node(path);if(i<0)return MINI_ERR_NOT_FOUND;
    if(nodes[i].type!=MINI_FS_TYPE_DIRECTORY)return MINI_ERR_NOT_DIR;
    size_t n=strlen(path);
    for(unsigned j=0;j<8;++j) if(nodes[j].path && !strncmp(nodes[j].path,path,n) && nodes[j].path[n]=='/')
        return MINI_ERR_NOT_EMPTY;
    nodes[i].path=NULL;return MINI_OK;
}
static mini_result_t tree_rename(const char *from,const char *to)
{
    int i=node(from),j=node(to);
    if(i<0)return MINI_ERR_NOT_FOUND;
    if(nodes[i].type!=MINI_FS_TYPE_FILE)return MINI_ERR_IS_DIR;
    if(i==j)return MINI_OK;
    if(j>=0){if(nodes[j].type!=MINI_FS_TYPE_FILE)return MINI_ERR_IS_DIR;nodes[j].path=NULL;}
    nodes[i].path=to;return MINI_OK;
}
static void tree_tests(void)
{
    const mini_fs_api_t tree={.mkdir=tree_mkdir,.remove_file=tree_remove,.rmdir=tree_rmdir,.rename=tree_rename};
    nodes[0].path="/sd/a";nodes[0].type=MINI_FS_TYPE_FILE;nodes[0].content=1;
    nodes[1].path="/sd/b";nodes[1].type=MINI_FS_TYPE_FILE;nodes[1].content=2;
    nodes[2].path="/sd/d";nodes[2].type=MINI_FS_TYPE_DIRECTORY;
    nodes[3].path="/sd/d/c";nodes[3].type=MINI_FS_TYPE_FILE;
    assert(webfs_mutate(&tree,WEBFS_MKDIR,"/sd/new",NULL)==MINI_OK);
    assert(node("/sd/new")>=0);
    assert(webfs_mutate(&tree,WEBFS_MKDIR,"/sd/new",NULL)==MINI_ERR_EXISTS);
    assert(webfs_mutate(&tree,WEBFS_RENAME,"/sd/a","/sd/a")==MINI_OK && nodes[0].content==1);
    assert(webfs_mutate(&tree,WEBFS_RENAME,"/sd/a","/sd/b")==MINI_OK);
    assert(node("/sd/a")==-1 && node("/sd/b")==0 && nodes[0].content==1 && !nodes[1].path);
    assert(webfs_mutate(&tree,WEBFS_RENAME,"/sd/b","/sd/c")==MINI_OK);
    assert(node("/sd/b")==-1 && node("/sd/c")==0 && nodes[0].content==1);
    assert(webfs_mutate(&tree,WEBFS_RENAME,"/sd/c","/sd/d")==MINI_ERR_IS_DIR);
    assert(webfs_mutate(&tree,WEBFS_RENAME,"/sd/d","/sd/e")==MINI_ERR_IS_DIR);
    assert(webfs_mutate(&tree,WEBFS_RENAME,"/sd/d","/sd/d")==MINI_ERR_IS_DIR);
    assert(webfs_mutate(&tree,WEBFS_REMOVE_FILE,"/sd/d",NULL)==MINI_ERR_IS_DIR);
    assert(webfs_mutate(&tree,WEBFS_RMDIR,"/sd/d",NULL)==MINI_ERR_NOT_EMPTY);
    assert(node("/sd/d/c")==3 && node("/sd/d")==2 && node("/sd/c")==0);
    assert(webfs_mutate(&tree,WEBFS_RMDIR,"/sd/c",NULL)==MINI_ERR_NOT_DIR);
    assert(webfs_mutate(&tree,WEBFS_REMOVE_FILE,"/sd/d/c",NULL)==MINI_OK);
    assert(webfs_mutate(&tree,WEBFS_RMDIR,"/sd/d",NULL)==MINI_OK);
    assert(webfs_mutate(&tree,WEBFS_RMDIR,"/sd/new",NULL)==MINI_OK);
    assert(node("/sd/d/c")==-1 && node("/sd/d")==-1 && node("/sd/new")==-1);
}

int main(void) { uploads();namespace_tests();tree_tests();puts("adv_webfs_mutations: PASS");return 0; }

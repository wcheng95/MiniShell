#include "adv_filesystem_space.h"
#include <assert.h>
#include <stdio.h>

static unsigned calls;
static mini_result_t error;
static char root[8];
static mini_result_t info(const char *path, uint64_t *total, uint64_t *free_bytes)
{
    ++calls; strcpy(root,path);
    *total=!strcmp(path,"/flash") ? 1000 : 10000000000ULL;
    *free_bytes=*total-100;
    return error;
}
int main(void)
{
    const char *paths[]={"/flash","/flash/ft8","/sd","/sd/foo/bar"};
    for (unsigned i=0;i<4;++i) {
        uint64_t total=0,free_bytes=0;
        assert(adv_fs_volume_space(paths[i],true,true,info,&total,&free_bytes)==MINI_OK);
        assert(!strcmp(root,i<2 ? "/flash" : "/sd"));
        assert(total==(i<2 ? 1000 : 10000000000ULL) && free_bytes==total-100);
    }
    uint64_t total,free_bytes;
    assert(adv_fs_volume_space("/sd",true,false,info,&total,&free_bytes)==MINI_ERR_NOT_FOUND);
    assert(adv_fs_volume_space("/sd/foo/bar",true,false,info,&total,&free_bytes)==MINI_ERR_NOT_FOUND);
    assert(adv_fs_volume_space("/flash",false,true,info,&total,&free_bytes)==MINI_ERR_NOT_FOUND);
    const char *bad[]={"/flashy","/sda/x","/","relative",NULL};
    for (unsigned i=0;i<sizeof(bad)/sizeof(*bad);++i)
        assert(adv_fs_volume_space(bad[i],true,true,info,&total,&free_bytes)==MINI_ERR_NOT_FOUND);
    assert(calls==4);
    const mini_result_t errors[]={MINI_ERR_IO,MINI_ERR_NOT_READY,MINI_ERR_NOT_FOUND};
    for (unsigned i=0;i<3;++i) {
        error=errors[i];
        assert(adv_fs_volume_space("/sd/foo/bar",true,true,info,&total,&free_bytes)==error);
    }
    puts("adv_filesystem_space: PASS");
    return 0;
}

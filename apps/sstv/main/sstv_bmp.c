#include "sstv_bmp.h"

#include <string.h>

static void put16(uint8_t *p, uint16_t v)
{
    p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8);
}
static void put32(uint8_t *p, uint32_t v)
{
    p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24);
}
static int write_all(SstvBmpSink *b, const void *data, uint32_t size)
{
    const uint8_t *p=data;
    while(size) {
        uint32_t n=0;
        if(b->fs->write(b->file,p,size,&n)!=MINI_OK || !n || n>size) return -1;
        p+=n; size-=n;
    }
    return 0;
}
static void discard(SstvBmpSink *b)
{
    if(b->active) {
        (void)b->fs->close(b->file);
        b->active=0;
    }
    if(b->path && b->fs->remove_file) (void)b->fs->remove_file(b->path);
}
static int bmp_begin(void *ctx, unsigned width, unsigned height)
{
    SstvBmpSink *b=ctx;
    if(width!=SSTV_ROBOT36_WIDTH || height!=SSTV_ROBOT36_HEIGHT) return -1;
    if(b->fs->open(b->path,MINI_FS_WRITE|MINI_FS_CREATE|MINI_FS_TRUNC,&b->file)!=MINI_OK) {
        b->failed=1; return -1;
    }
    b->active=1;
    uint8_t h[54]; memset(h,0,sizeof(h));
    h[0]='B'; h[1]='M';
    put32(h+2,54u+width*height*3u);
    put32(h+10,54u); put32(h+14,40u); put32(h+18,width);
    put32(h+22,(uint32_t)(-(int32_t)height));
    put16(h+26,1u); put16(h+28,24u); put32(h+34,width*height*3u);
    if(write_all(b,h,sizeof(h))) { b->failed=1; discard(b); return -1; }
    return 0;
}
static int bmp_row(void *ctx, unsigned y, const uint8_t *rgb, unsigned width)
{
    SstvBmpSink *b=ctx;
    if(!b->active || y!=b->rows || width!=SSTV_ROBOT36_WIDTH) return -1;
    for(unsigned x=0;x<width;++x) {
        b->bgr[3u*x+0u]=rgb[3u*x+2u];
        b->bgr[3u*x+1u]=rgb[3u*x+1u];
        b->bgr[3u*x+2u]=rgb[3u*x+0u];
    }
    if(write_all(b,b->bgr,width*3u)) { b->failed=1; discard(b); return -1; }
    ++b->rows;
    return 0;
}
static int bmp_end(void *ctx, int complete)
{
    SstvBmpSink *b=ctx;
    if(!b->active) return b->failed ? -1 : 0;
    if(!complete || b->rows!=SSTV_ROBOT36_HEIGHT) { discard(b); return complete ? -1 : 0; }
    int error=0;
    if(b->fs->sync && b->fs->sync(b->file)!=MINI_OK) error=1;
    if(b->fs->close(b->file)!=MINI_OK) error=1;
    b->active=0;
    if(error) { b->failed=1; if(b->fs->remove_file) (void)b->fs->remove_file(b->path); return -1; }
    return 0;
}
void sstv_bmp_sink_init(SstvBmpSink *b, const mini_fs_api_t *fs, const char *path,
                        SstvImageSink *sink)
{
    memset(b,0,sizeof(*b)); b->fs=fs; b->path=path;
    sink->ctx=b; sink->begin=bmp_begin; sink->row=bmp_row; sink->end=bmp_end;
}
void sstv_bmp_abort(SstvBmpSink *b)
{
    if(b && b->active) discard(b);
}

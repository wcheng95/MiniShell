#include "sstv_core.h"
#include "sstv_wav.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { FILE *fp; } Slot;
static Slot slots[4];

static mini_result_t f_open(const char *path,uint32_t flags,mini_file_t *out)
{
    const char *mode=(flags&MINI_FS_WRITE)?"wb":"rb";
    for(unsigned i=1;i<4;++i) if(!slots[i].fp) {
        slots[i].fp=fopen(path,mode);
        if(!slots[i].fp) return -1;
        *out=i;
        return MINI_OK;
    }
    return -1;
}
static mini_result_t f_close(mini_file_t f)
{
    if(f>=4||!slots[f].fp) return -1;
    int r=fclose(slots[f].fp);
    slots[f].fp=NULL;
    return r?-1:MINI_OK;
}
static mini_result_t f_read(mini_file_t f,void *p,uint32_t n,uint32_t *got)
{
    if(f>=4||!slots[f].fp) return -1;
    *got=(uint32_t)fread(p,1,n,slots[f].fp);
    return ferror(slots[f].fp)?-1:MINI_OK;
}
static mini_result_t f_write(mini_file_t f,const void*p,uint32_t n,uint32_t *put)
{
    if(f>=4||!slots[f].fp) return -1;
    *put=(uint32_t)fwrite(p,1,n,slots[f].fp);
    return *put==n?MINI_OK:-1;
}
static mini_result_t f_seek(mini_file_t f,int64_t n,uint32_t origin,uint64_t *pos)
{
    if(f>=4||!slots[f].fp||origin!=MINI_FS_SEEK_SET||
       fseek(slots[f].fp,(long)n,SEEK_SET)) return -1;
    *pos=(uint64_t)ftell(slots[f].fp);
    return MINI_OK;
}
static mini_result_t f_sync(mini_file_t f)
{
    return (f<4&&slots[f].fp&&fflush(slots[f].fp)==0)?MINI_OK:-1;
}
static mini_result_t f_remove(const char *path)
{
    return remove(path)==0?MINI_OK:-1;
}

static const mini_fs_api_t fs={
    .open=f_open,.close=f_close,.read=f_read,.write=f_write,.seek=f_seek,
    .sync=f_sync,.remove_file=f_remove
};

typedef struct { FILE *fp; unsigned rows; } RawSink;
static int raw_begin(void *v,unsigned w,unsigned h)
{
    (void)v;
    return (w==320u&&h==240u)?0:-1;
}
static int raw_row(void *v,unsigned y,const uint8_t *rgb,unsigned w)
{
    RawSink*s=v;
    if(y!=s->rows||w!=320u) return -1;
    if(fwrite(rgb,1,w*3u,s->fp)!=w*3u) return -1;
    ++s->rows;
    return 0;
}
static int raw_end(void *v,int complete)
{
    RawSink*s=v;
    (void)complete;
    (void)s;
    return 0;
}

static int core_decode(const char *in,const char *out,unsigned chunk)
{
    FILE *fp=fopen(in,"rb");
    FILE *op=fopen(out,"wb");
    assert(fp&&op&&chunk>0&&chunk<=4096u);
    unsigned char hdr[44];
    assert(fread(hdr,1,sizeof(hdr),fp)==sizeof(hdr));

    RawSink rs={op,0};
    SstvImageSink sink={&rs,raw_begin,raw_row,raw_end};
    SstvCore core;
    sstv_core_init(&core,&sink);
    int16_t *pcm=malloc(chunk*sizeof(*pcm));
    assert(pcm);
    size_t n;
    while((n=fread(pcm,sizeof(*pcm),chunk,fp))!=0u)
        (void)sstv_core_process(&core,pcm,n);
    free(pcm);
    fclose(fp);
    SstvResult r=sstv_core_finish(&core);
    fclose(op);
    if(r!=SSTV_RESULT_COMPLETE||rs.rows!=240u) {
        fprintf(stderr,"core: %s rows=%u\n",sstv_result_string(r),rs.rows);
        return 1;
    }
    return 0;
}
int main(int argc,char **argv)
{
    assert(sizeof(SstvCore)<12000u);
    if(argc==1) {
        SstvCore c;
        sstv_core_init(&c,NULL);
        assert(c.state==SSTV_STATE_LEADER);
        puts("sstv unit: PASS");
        return 0;
    }
    if(argc==5 && !strcmp(argv[1],"core"))
        return core_decode(argv[2],argv[3],(unsigned)strtoul(argv[4],NULL,10));
    if(argc==4 && !strcmp(argv[1],"wav")) {
        const char *e=sstv_wav_decode(&fs,argv[2],argv[3]);
        if(e) {
            fputs(e,stderr);
            return 1;
        }
        return 0;
    }
    return 2;
}

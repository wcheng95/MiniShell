#include "rtty_core.h"
#include "rtty_ita2.h"
#include "rtty_wav.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static FILE *input;
static int fault;
static unsigned closed;
static mini_result_t open_file(const char *path, uint32_t flags, mini_file_t *f)
{
    assert(flags==MINI_FS_READ);
    input=fopen(path,"rb"); if(!input) return MINI_ERR_IO;
    *f=1; return 0;
}
static mini_result_t close_file(mini_file_t f)
{
    assert(f==1); ++closed;
    return fclose(input) || fault==3 ? MINI_ERR_IO : 0;
}
static mini_result_t read_file(mini_file_t f, void *p, uint32_t n, uint32_t *got)
{
    assert(f==1);
    if(fault==1) return MINI_ERR_IO;
    if(n>7) n=7;
    *got=(uint32_t)fread(p,1,n,input);
    return ferror(input)?MINI_ERR_IO:0;
}
static mini_result_t seek_file(mini_file_t f, int64_t n, uint32_t origin, uint64_t *pos)
{
    assert(f==1 && origin==MINI_FS_SEEK_SET);
    if(fault==2 || fseek(input,(long)n,SEEK_SET)) return MINI_ERR_IO;
    *pos=(uint64_t)ftell(input); return 0;
}
static void say(const char *s) { fputs(s,stdout); }
static void emit(void *ctx,char c) { (void)ctx; putchar(c); }
int main(int argc, char **argv)
{
    if(argc==1) {
        uint8_t figures=0;
        assert(rtty_ita2_decode(&figures,3)=='A');
        assert(!rtty_ita2_decode(&figures,27) && figures);
        assert(rtty_ita2_decode(&figures,23)=='1');
        assert(rtty_ita2_decode(&figures,4)==' ' && figures);
        assert(rtty_ita2_decode(&figures,8)=='\r' && rtty_ita2_decode(&figures,2)=='\n');
        assert(!rtty_ita2_decode(&figures,31) && !figures);
        assert(!rtty_ita2_decode(&figures,99));
        assert(RTTY_SAMPLES_PER_BIT>264.02 && RTTY_SAMPLES_PER_BIT<264.03);
        RttyCore core; rtty_core_init(&core); int16_t silence[1200]={0};
        for(unsigned i=0;i<50;++i) rtty_core_process(&core,silence,1200,emit,NULL);
        assert(!core.locked);
        printf("rtty unit: PASS state=%zu samples_per_bit=%.9f\n",sizeof(core),RTTY_SAMPLES_PER_BIT);
        return 0;
    }
    if(!strcmp(argv[1],"core")) {
        assert(argc==4);
        FILE *f=fopen(argv[2],"rb"); assert(f);
        unsigned chunk=(unsigned)atoi(argv[3]); assert(chunk && chunk<=4096);
        RttyCore core; rtty_core_init(&core);
        unsigned char bytes[8192]; int16_t pcm[4096]; size_t n;
        while((n=fread(bytes,2,chunk,f))) {
            for(size_t i=0;i<n;++i) {
                unsigned v=bytes[2*i]|(unsigned)bytes[2*i+1]<<8;
                pcm[i]=(int16_t)(v>=32768?(int)v-65536:(int)v);
            }
            rtty_core_process(&core,pcm,n,emit,NULL);
        }
        assert(!ferror(f)); fclose(f); return 0;
    }
    assert(argc>=3);
    if(argc==4) fault=atoi(argv[3]);
    mini_fs_api_t fs={.open=open_file,.close=close_file,.read=read_file,.seek=seek_file};
    mini_console_api_t console={.write=say};
    const char *error=rtty_wav_decode(&fs,&console,argv[2]);
    assert(closed==1);
    if(error) { fputs(error,stderr); return 1; }
    return 0;
}

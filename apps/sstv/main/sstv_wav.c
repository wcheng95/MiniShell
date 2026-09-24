#include "sstv_wav.h"
#include "sstv_bmp.h"
#include "sstv_core.h"

#include <string.h>

static uint32_t u32(const uint8_t *p) { return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static unsigned u16(const uint8_t *p) { return p[0]|(unsigned)p[1]<<8; }
static int read_exact(const mini_fs_api_t *fs, mini_file_t f, void *buffer, uint32_t n)
{
    uint8_t *p=buffer;
    while(n) {
        uint32_t got=0;
        if(fs->read(f,p,n,&got)!=MINI_OK || !got || got>n) return -1;
        p+=got; n-=got;
    }
    return 0;
}
static int32_t pcm_sample(const uint8_t *p, unsigned width)
{
    uint32_t v=u16(p);
    if(width==3u) v|=(uint32_t)p[2]<<16;
    int32_t s=(v&(1u<<(width*8u-1u))) ? (int32_t)v-(int32_t)(1u<<(width*8u)) : (int32_t)v;
    return width==3u ? s/256 : s;
}
static const char *result_error(SstvResult r)
{
    switch(r) {
    case SSTV_RESULT_UNSUPPORTED: return "sstv: unsupported VIS mode\n";
    case SSTV_RESULT_BAD_VIS: return "sstv: invalid VIS header\n";
    case SSTV_RESULT_SINK_ERROR: return "sstv: cannot write BMP\n";
    case SSTV_RESULT_TRUNCATED: return "sstv: incomplete SSTV image\n";
    default: return "sstv: decode error\n";
    }
}
const char *sstv_wav_decode(const mini_fs_api_t *fs, const char *input_path, const char *output_path)
{
    mini_file_t file=MINI_FILE_INVALID;
    if(fs->open(input_path,MINI_FS_READ,&file)!=MINI_OK) return "sstv: cannot open WAV\n";
    const char *error="sstv: malformed or truncated WAV\n";
    uint8_t h[16]; uint64_t pos=12,end=0; unsigned channels=0,width=0,align=0; int format=0;
    if(read_exact(fs,file,h,12) || memcmp(h,"RIFF",4) || memcmp(h+8,"WAVE",4)) goto done;
    end=(uint64_t)u32(h+4)+8u;
    while(pos+8u<=end) {
        if(read_exact(fs,file,h,8)) goto done;
        uint32_t length=u32(h+4); pos+=8u;
        uint64_t next=pos+(uint64_t)length+(length&1u);
        if(pos+(uint64_t)length>end) goto done;
        if(!memcmp(h,"fmt ",4)) {
            if(format || length<16u || read_exact(fs,file,h,16)) goto done;
            channels=u16(h+2); width=u16(h+14)/8u; align=u16(h+12);
            if(u16(h)!=1u || (channels!=1u && channels!=2u) || (u16(h+14)!=16u && u16(h+14)!=24u)) {
                error="sstv: unsupported WAV PCM format\n"; goto done;
            }
            if(u32(h+4)!=SSTV_SAMPLE_RATE) { error="sstv: WAV sample rate must be 12000 Hz\n"; goto done; }
            if(align!=channels*width || u32(h+8)!=SSTV_SAMPLE_RATE*align) goto done;
            format=1;
        } else if(!memcmp(h,"data",4)) {
            if(!format || !align || length%align) goto done;
            SstvBmpSink bmp; SstvImageSink sink; sstv_bmp_sink_init(&bmp,fs,output_path,&sink);
            SstvCore core; sstv_core_init(&core,&sink);
            uint8_t bytes[1536]; int16_t pcm[256];
            SstvResult result=SSTV_RESULT_RUNNING;
            while(length && result==SSTV_RESULT_RUNNING) {
                uint32_t frames=length/align; if(frames>256u) frames=256u;
                if(read_exact(fs,file,bytes,frames*align)) { sstv_bmp_abort(&bmp); goto done; }
                for(uint32_t i=0;i<frames;++i) {
                    int32_t v=pcm_sample(bytes+i*align,width);
                    if(channels==2u) v=(v+pcm_sample(bytes+i*align+width,width))/2;
                    pcm[i]=(int16_t)v;
                }
                result=sstv_core_process(&core,pcm,frames);
                length-=frames*align;
            }
            if(result==SSTV_RESULT_RUNNING) result=sstv_core_finish(&core);
            if(result!=SSTV_RESULT_COMPLETE) { sstv_bmp_abort(&bmp); error=result_error(result); goto done; }
            error=NULL; goto done;
        }
        uint64_t actual=0;
        if(next>end || fs->seek(file,(int64_t)next,MINI_FS_SEEK_SET,&actual)!=MINI_OK || actual!=next) goto done;
        pos=next;
    }
done:
    if(fs->close(file)!=MINI_OK && !error) error="sstv: WAV close error\n";
    return error;
}

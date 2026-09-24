#include "rtty_wav.h"
#include "rtty_core.h"
#include <string.h>
static uint32_t u32(const uint8_t *p) { return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static unsigned u16(const uint8_t *p) { return p[0]|(unsigned)p[1]<<8; }
static int read_exact(const mini_fs_api_t *fs, mini_file_t f, void *buffer, uint32_t n)
{
    uint8_t *p=buffer;
    while(n) {
        uint32_t got=0;
        if(fs->read(f,p,n,&got) || !got || got>n) return -1;
        p+=got; n-=got;
    }
    return 0;
}
static void emit(void *ctx, char c)
{
    const mini_console_api_t *console=ctx; char text[2]={c,0}; console->write(text);
}
static int32_t sample(const uint8_t *p, unsigned width)
{
    uint32_t v=u16(p);
    if(width==3) v|=(uint32_t)p[2]<<16;
    int32_t signed_value=(v & (1u<<(width*8-1))) ? (int32_t)v-(int32_t)(1u<<(width*8)) : (int32_t)v;
    return width==3 ? signed_value/256 : signed_value;
}
const char *rtty_wav_decode(const mini_fs_api_t *fs, const mini_console_api_t *console, const char *path)
{
    mini_file_t file=0;
    if(fs->open(path,MINI_FS_READ,&file)) return "rtty: cannot open WAV\n";
    const char *error="rtty: malformed or truncated WAV\n";
    uint8_t h[16]; uint64_t pos=12, end; unsigned channels=0,width=0,align=0; int format=0;
    if(read_exact(fs,file,h,12) || memcmp(h,"RIFF",4) || memcmp(h+8,"WAVE",4)) goto done;
    end=(uint64_t)u32(h+4)+8;
    while(pos+8<=end) {
        if(read_exact(fs,file,h,8)) goto done;
        uint32_t length=u32(h+4); pos+=8;
        uint64_t next=pos+(uint64_t)length+(length&1);
        if(pos+(uint64_t)length>end) goto done;
        if(!memcmp(h,"fmt ",4)) {
            if(format || length<16 || read_exact(fs,file,h,16)) goto done;
            channels=u16(h+2); width=u16(h+14)/8; align=u16(h+12);
            if(u16(h)!=1 || (channels!=1 && channels!=2) || (u16(h+14)!=16 && u16(h+14)!=24)) {
                error="rtty: unsupported WAV PCM format\n"; goto done;
            }
            if(u32(h+4)!=12000) { error="rtty: WAV sample rate must be 12000 Hz\n"; goto done; }
            if(align!=channels*width || u32(h+8)!=12000*align) goto done;
            format=1;
        } else if(!memcmp(h,"data",4)) {
            if(!format || length%align) goto done;
            RttyCore core; rtty_core_init(&core);
            uint8_t bytes[1536]; int16_t pcm[256];
            while(length) {
                uint32_t frames=length/align; if(frames>256) frames=256;
                if(read_exact(fs,file,bytes,frames*align)) goto done;
                for(uint32_t i=0;i<frames;++i) {
                    int32_t v=sample(bytes+i*align,width);
                    if(channels==2) v=(v+sample(bytes+i*align+width,width))/2;
                    pcm[i]=(int16_t)v;
                }
                rtty_core_process(&core,pcm,frames,emit,(void *)console);
                length-=frames*align;
            }
            error=NULL; goto done;
        }
        uint64_t actual;
        if(next>end || fs->seek(file,(int64_t)next,MINI_FS_SEEK_SET,&actual) || actual!=next) goto done;
        pos=next;
    }
done:
    if(fs->close(file) && !error) error="rtty: close error\n";
    return error;
}

#include "rtty_core.h"
#include "rtty_ita2.h"
#include <math.h>
#include <string.h>

static float coefficient(float hz) { return 2.0f*cosf(6.283185307179586f*hz/12000.0f); }
static float energy(const int16_t *pcm, unsigned n, unsigned head, float c)
{
    float a=0, b=0;
    for (unsigned i=0;i<n;++i) {
        float v=(float)pcm[(head+i)%n]/32768.0f+c*a-b;
        b=a; a=v;
    }
    return a*a+b*b-c*a*b;
}
void rtty_core_init(RttyCore *s) { memset(s,0,sizeof(*s)); s->previous=1; }
static void acquire(RttyCore *s)
{
    float total=0, best=0, hz=0;
    for(unsigned i=0;i<1200;++i) { float x=s->acquisition[i]/32768.0f; total+=x*x; }
    /* Idle MARK seeds acquisition. Both inferred tones stay in the fixed band. */
    for(unsigned f=670;f<=1500;f+=5) {
        float e=energy(s->acquisition,1200,0,coefficient((float)f));
        if(e>best) { best=e; hz=(float)f; }
    }
    s->fill=0;
    if(total < 0.001f || best < total*1200*0.2f) return;
    s->mark_hz=hz; s->coefficient[0]=coefficient(hz-170); s->coefficient[1]=coefficient(hz);
    s->locked=1; s->previous=1; s->since_valid=0;
    memset(s->history,0,sizeof(s->history)); s->head=s->tick=0;
}
static void decision(RttyCore *s, unsigned mark, RttyCharacter emit, void *ctx)
{
    if(!s->framing) {
        if(s->previous && !mark) {
            s->framing=1; s->stage=s->code=0; s->remaining=RTTY_SAMPLES_PER_BIT*0.5;
        }
    } else {
        s->remaining-=12;
        if(s->remaining<=0) {
            if(s->stage==0 && mark) s->framing=0;
            else if(s->stage>=1 && s->stage<=5) s->code |= mark<<(s->stage-1);
            else if(s->stage>=6 && !mark) s->framing=0;
            else if(s->stage==7) {
                char ch=rtty_ita2_decode(&s->figures,s->code);
                if(ch && emit) emit(ctx,ch);
                s->since_valid=0; s->framing=0;
            }
            ++s->stage;
            /* Stop samples at 6.5 and 7.0 bit times accept 1.5+ stop bits. */
            s->remaining += RTTY_SAMPLES_PER_BIT*(s->stage==7?0.5:1.0);
        }
    }
    s->previous=mark;
}
void rtty_core_process(RttyCore *s, const int16_t *pcm, size_t count, RttyCharacter emit, void *ctx)
{
    if(!s || (!pcm && count)) return;
    for(size_t i=0;i<count;++i) {
        if(!s->locked) {
            s->acquisition[s->fill++]=pcm[i];
            if(s->fill==1200) acquire(s);
            continue;
        }
        s->history[s->head]=pcm[i]; s->head=(s->head+1)%120;
        if(++s->tick==12) {
            s->tick=0;
            float space=energy(s->history,120,s->head,s->coefficient[0]);
            float mark=energy(s->history,120,s->head,s->coefficient[1]);
            decision(s,mark>=space,emit,ctx);
        }
        if(++s->since_valid>=36000) {
            s->locked=s->framing=s->fill=0; s->figures=0;
        }
    }
}

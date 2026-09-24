#!/usr/bin/env python3
"""Generate a deterministic standards-shaped Robot 36 SSTV test WAV using stdlib only."""
import argparse
import math
import wave

FS=12000
W=320
H=240
VIS=8

BARS=[
    (255,255,255),(255,255,0),(0,255,255),(0,255,0),
    (255,0,255),(255,0,0),(0,0,255),(0,0,0),
]

def clamp(v):
    return max(0,min(255,int(round(v))))

def rgb(x,y):
    if y >= 208:
        v=round(255*x/(W-1))
        return (v,v,v)
    return BARS[min(7,x//40)]

def ycbcr(r,g,b):
    y=0.299*r+0.587*g+0.114*b
    cb=128.0-0.168736*r-0.331264*g+0.5*b
    cr=128.0+0.5*r-0.418688*g-0.081312*b
    return clamp(y),clamp(cb),clamp(cr)

def value_freq(v):
    return 1500.0+800.0*v/255.0

def samples(vis=VIS,bad_parity=False):
    phase=0.0
    emitted=0
    endpoint=0.0
    def tone(freq,ms):
        nonlocal phase,emitted,endpoint
        endpoint += FS*ms/1000.0
        stop=round(endpoint)
        while emitted < stop:
            phase=(phase+2*math.pi*freq/FS)%(2*math.pi)
            yield 0.55*math.sin(phase)
            emitted+=1
    yield from tone(1900,300)
    yield from tone(1200,10)
    yield from tone(1900,300)
    yield from tone(1200,30)
    ones=0
    for bitn in range(7):
        bit=(vis>>bitn)&1
        ones+=bit
        yield from tone(1100 if bit else 1300,30)
    parity=(ones&1) ^ bool(bad_parity)
    yield from tone(1100 if parity else 1300,30)
    yield from tone(1200,30)
    for y in range(H):
        row=[ycbcr(*rgb(x,y)) for x in range(W)]
        yield from tone(1200,9)
        yield from tone(1500,3)
        for p in row:
            yield from tone(value_freq(p[0]),88/W)
        yield from tone(1500 if y%2==0 else 2300,4.5)
        yield from tone(1900,1.5)
        chan=2 if y%2==0 else 1
        for p in row:
            yield from tone(value_freq(p[chan]),44/W)
    yield from tone(1900,100)

def write(path,width=2,channels=1,vis=VIS,bad_parity=False):
    with wave.open(str(path),'wb') as w:
        w.setparams((channels,width,FS,0,'NONE','not compressed'))
        buf=bytearray()
        scale=(1<<(8*width-1))-1
        for s in samples(vis,bad_parity):
            v=round(s*scale)
            b=v.to_bytes(width,'little',signed=True)
            buf += b*channels
            if len(buf)>=8192:
                w.writeframesraw(buf); buf.clear()
        w.writeframes(buf)

def main():
    p=argparse.ArgumentParser()
    p.add_argument('path')
    p.add_argument('--width',type=int,choices=(2,3),default=2)
    p.add_argument('--channels',type=int,choices=(1,2),default=1)
    p.add_argument('--vis',type=int,default=VIS)
    p.add_argument('--bad-parity',action='store_true')
    a=p.parse_args(); write(a.path,a.width,a.channels,a.vis,a.bad_parity)

if __name__=='__main__': main()

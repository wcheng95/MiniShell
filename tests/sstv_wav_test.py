#!/usr/bin/env python3
"""Robot 36 WAV/core/BMP and MiniShell runtime regressions."""
import importlib.util
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import wave

unit, shell, appdir, root = sys.argv[1:]
spec=importlib.util.spec_from_file_location(
    "generator",Path(root)/"apps/sstv/tools/generate_test_wav.py")
gen=importlib.util.module_from_spec(spec)
spec.loader.exec_module(gen)

def run(*args, **kwargs):
    return subprocess.run([str(x) for x in args],capture_output=True,timeout=90,**kwargs)

def bmp_info(path):
    data=Path(path).read_bytes()
    assert data[:2]==b"BM"
    assert struct.unpack_from("<I",data,10)[0]==54
    w=struct.unpack_from("<i",data,18)[0]
    h=struct.unpack_from("<i",data,22)[0]
    bpp=struct.unpack_from("<H",data,28)[0]
    assert (w,h,bpp)==(320,-240,24)
    assert len(data)==54+320*240*3
    return data

def pixel(data,x,y):
    o=54+(y*320+x)*3
    b,g,r=data[o:o+3]
    return (r,g,b)

def check_pixels(data):
    for y in (10,100,207):
        for x in (20,60,100,140,180,220,260,300):
            got=pixel(data,x,y)
            exp=gen.rgb(x,y)
            assert max(abs(a-b) for a,b in zip(got,exp))<=18,(x,y,got,exp)
    for y in (220,239):
        for x in (20,100,180,260,300):
            got=pixel(data,x,y)
            exp=gen.rgb(x,y)
            assert max(abs(a-b) for a,b in zip(got,exp))<=18,(x,y,got,exp)

with tempfile.TemporaryDirectory(prefix="sstv-") as name:
    tmp=Path(name)
    flash=tmp/"flash"
    flash.mkdir()
    wav=flash/"test.wav"
    bmp=flash/"test.bmp"
    gen.write(wav,2,1)
    with wave.open(str(wav),"rb") as w:
        assert w.getframerate()==12000
        assert w.getnchannels()==1
        assert w.getsampwidth()==2

    reference=None
    for chunk in (1,137,4096):
        raw=tmp/f"core-{chunk}.rgb"
        r=run(unit,"core",wav,raw,chunk)
        assert r.returncode==0,r
        data=raw.read_bytes()
        assert len(data)==320*240*3
        reference=data if reference is None else reference
        assert data==reference

    for width,channels in ((2,1),(2,2),(3,1),(3,2)):
        gen.write(wav,width,channels)
        r=run(unit,"wav",wav,bmp)
        assert r.returncode==0,r
        check_pixels(bmp_info(bmp))

    good=wav.read_bytes()
    junk=b"JUNK"+struct.pack("<I",3)+b"abc\0"
    extended=bytearray(good[:12]+junk+good[12:36]+b"\0\0"+good[36:])
    struct.pack_into("<I",extended,4,len(extended)-8)
    struct.pack_into("<I",extended,28,18)
    wav.write_bytes(extended)
    r=run(unit,"wav",wav,bmp)
    assert r.returncode==0,r

    gen.write(wav,2,1,bad_parity=True)
    bmp.unlink(missing_ok=True)
    r=run(unit,"wav",wav,bmp)
    assert r.returncode!=0 and b"invalid VIS" in r.stderr and not bmp.exists(),r

    gen.write(wav,2,1,vis=44)
    bmp.unlink(missing_ok=True)
    r=run(unit,"wav",wav,bmp)
    assert r.returncode!=0 and b"unsupported VIS" in r.stderr and not bmp.exists(),r

    gen.write(wav,3,2)
    good=wav.read_bytes()
    cases=[]
    for offset,value,fmt,needle in (
        (24,48000,"<I",b"12000 Hz"),
        (20,3,"<H",b"unsupported"),
        (22,3,"<H",b"unsupported"),
        (34,8,"<H",b"unsupported"),
        (32,1,"<H",b"malformed"),
        (28,1,"<I",b"malformed"),
    ):
        data=bytearray(good)
        struct.pack_into(fmt,data,offset,value)
        cases.append((bytes(data),needle))
    cases += [(b"not a WAV",b"malformed"),(good[:-1000],b"truncated")]
    for data,needle in cases:
        wav.write_bytes(data)
        bmp.unlink(missing_ok=True)
        r=run(unit,"wav",wav,bmp)
        assert r.returncode!=0 and needle in r.stderr,(needle,r)
        assert not bmp.exists()

    gen.write(wav,2,1)
    env=dict(os.environ,MINISHELL_ROOT=str(tmp),MINISHELL_APP_DIR=appdir)
    r=subprocess.run(
        [shell],
        input=b"sstv /flash/test.wav /flash/test.bmp\nexit\n",
        env=env,capture_output=True,timeout=90)
    assert r.returncode==0,r
    assert b"sstv: Robot 36 image decoded" in r.stdout,r
    check_pixels(bmp_info(bmp))
    print(r.stdout.decode(),end="")
print("sstv_wav: Robot36/VIS/chunks/PCM/BMP/malformed/runtime PASS")

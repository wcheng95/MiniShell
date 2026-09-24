#!/usr/bin/env python3
"""Offline pure-core, public-FS adapter and actual MiniShell loader regressions."""
import importlib.util
import math
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import wave

unit, shell, appdir, root = sys.argv[1:]
spec = importlib.util.spec_from_file_location("generator", Path(root)/"apps/rtty/tools/generate_wav.py")
gen = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gen)
message = "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 12345\r\nRY 67890 -'$!&#(),./:;?\"\r\n"

def run(*args):
    return subprocess.run([str(x) for x in args], capture_output=True, timeout=30)

with tempfile.TemporaryDirectory(prefix="rtty-") as name:
    tmp = Path(name)
    flash = tmp/"flash"
    flash.mkdir()
    # Full table/shift text at grid and off-grid frequencies, including band edges.
    for mark in (670, 672.5, 1000, 1232.5, 1400, 1500):
        for stop in (1.5, 2.0):
            raw = b"".join(round(x*32767).to_bytes(2,"little",signed=True)
                           for x in gen.samples(message, mark, stop))
            path = tmp/"core.raw"
            path.write_bytes(raw)
            for chunk in (1, 137, 4096):
                result = run(unit, "core", path, chunk)
                assert result.returncode == 0 and result.stdout == message.encode(), (mark, stop, chunk, result)
    # Independent frame fixture: a short false start and an invalid stop must
    # not emit characters; a subsequent valid E must still synchronize.
    segments = [(1,22), (0,0.15), (1,2)]
    segments += [(0,1)] + [((3 >> k)&1,1) for k in range(5)] + [(0,1.5),(1,3)]
    segments += [(0,1)] + [((1 >> k)&1,1) for k in range(5)] + [(1,15)]
    raw=bytearray(); phase=0.0; elapsed=0.0; index=0
    for bit,duration in segments:
        elapsed += duration*12000/45.45
        while index < math.ceil(elapsed):
            phase = (phase+2*math.pi*(1000 if bit else 830)/12000)%(2*math.pi)
            raw += round(16000*math.sin(phase)).to_bytes(2,"little",signed=True)
            index += 1
    path.write_bytes(raw)
    result=run(unit,"core",path,137)
    assert result.returncode==0 and result.stdout==b"E",result
    # Loss of useful framing expires lock; a later different idle pair reacquires.
    raw=b"".join(round(x*32767).to_bytes(2,"little",signed=True)
                 for x in gen.samples("ABC",1000))
    raw += bytes(4*12000*2)
    raw += b"".join(round(x*32767).to_bytes(2,"little",signed=True)
                   for x in gen.samples("XYZ",1400))
    path.write_bytes(raw)
    result=run(unit,"core",path,137)
    assert result.returncode==0 and result.stdout==b"ABCXYZ",result
    # All four supported format combinations, independent channel amplitudes.
    for width, channels in ((2,1), (2,2), (3,1), (3,2)):
        path = flash/"test.wav"
        gen.write(path, message, 1400, width, channels)
        if channels == 2:
            data = bytearray(path.read_bytes())
            for i in range(44, len(data), width*2):
                v = int.from_bytes(data[i:i+width], "little", signed=True)
                data[i+width:i+width*2] = (v//2).to_bytes(width, "little", signed=True)
            path.write_bytes(data)
        result = run(unit, "wav", path)
        assert result.returncode == 0 and result.stdout == message.encode(), result
    good = path.read_bytes()
    # Odd-sized ancillary chunks and extended fmt are skipped with RIFF padding.
    junk = b"JUNK"+struct.pack("<I",3)+b"abc\0"
    extended = bytearray(good[:12]+junk+good[12:36]+b"\0\0"+good[36:])
    struct.pack_into("<I", extended, 4, len(extended)-8)
    struct.pack_into("<I", extended, 28, 18)
    path.write_bytes(extended)
    result = run(unit, "wav", path)
    assert result.returncode == 0 and result.stdout == message.encode(), result
    for fault in (1,2,3):
        result = run(unit, "wav", path, fault)
        assert result.returncode != 0 and b"rtty:" in result.stderr, result
    bad = []
    for offset, value, fmt, error in (
        (24, 48000, "<I", b"12000 Hz"), (20, 3, "<H", b"unsupported"),
        (22, 3, "<H", b"unsupported"), (34, 8, "<H", b"unsupported"),
        (32, 1, "<H", b"malformed"), (28, 1, "<I", b"malformed"),
        (40, 0xffffffff, "<I", b"malformed"), (16, 15, "<I", b"malformed"),
    ):
        data = bytearray(good)
        struct.pack_into(fmt, data, offset, value)
        bad.append((data, error))
    bad += [(b"not a WAV",b"malformed"), (good[:-1],b"truncated"),
            (good[:20],b"truncated"), (good[:12],b"malformed")]
    for data, error in bad:
        path.write_bytes(data)
        result = run(unit, "wav", path)
        assert result.returncode != 0 and error in result.stderr, result
    # Runtime loader and public services, repeated app launches.
    text = "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG\r\n"
    gen.write(path, text)
    env = dict(os.environ, MINISHELL_ROOT=str(tmp), MINISHELL_APP_DIR=appdir)
    result = subprocess.run([shell], input=b"rtty /flash/test.wav\nrtty /flash/test.wav\nexit\n",
                            env=env, capture_output=True, timeout=30)
    assert result.returncode == 0 and result.stdout.count(text.encode()) == 2, result
    assert b"rtty:" not in result.stdout and not result.stderr, result
    print(result.stdout.decode(), end="")
    data=bytearray(path.read_bytes())
    struct.pack_into("<I",data,24,48000)
    path.write_bytes(data)
    result=subprocess.run([shell],input=b"rtty /flash/test.wav\nrtty\nexit\n",
                          env=env,capture_output=True,timeout=30)
    assert b"rtty: WAV sample rate must be 12000 Hz" in result.stdout,result
    assert b"app: rtty returned 3" in result.stdout,result
    assert b"usage: rtty <path.wav>" in result.stdout,result
    assert b"app: rtty returned 1" in result.stdout,result
print("rtty_wav: core/chunk/baud/band/ITA2/PCM/malformed/FS/runtime PASS")

#!/usr/bin/env python3
"""Actual MiniShell loading, public WAV/FS/Serial, real decode worker; no RF."""
import json
import math
import os
from pathlib import Path
import pty
import re
import select
import shutil
import struct
import subprocess
import sys
import tempfile
import threading
import wave

shell, appdir, tone_tool, decoder, root = sys.argv[1:]
root=Path(root)
def vectors(name):
    return re.findall(r'"([01]{75})"',(root/'tests'/name).read_text())
def flags(bits,n): return bits[:72]+format(n,'03b')
def pcm(signals):
    streams=[]
    for bits,hz in signals:
        tones=subprocess.check_output([tone_tool,'--tones',bits],text=True).strip()
        stream=[];phase=0
        for n in range(90000):
            value=0
            if 3000<=n<3000+79*960:
                value=math.sin(phase)
                phase=(phase+2*math.pi*(hz+6.25*int(tones[(n-3000)//960]))/6000)%(2*math.pi)
            stream.append(value)
        streams.append(stream)
    mono=bytearray()
    for n in range(90000):
        value=round(sum(s[n] for s in streams)*0.2*32767)
        mono+=struct.pack('<hh',value,value)
    return mono
with tempfile.TemporaryDirectory(prefix='js8-live-') as tmp:
    tmp=Path(tmp);flash=tmp/'flash';flash.mkdir();(flash/'js8chat').mkdir()
    shutil.copyfile(root/'apps/js8chat/resources/jsc.dict',flash/'js8chat/jsc.dict')
    direct=vectors('js8_directed_vectors.h');huff=vectors('js8_huffman_vectors.h')
    jsc=vectors('js8_jsc_vectors.h');hb=vectors('js8_compound_vectors.h')[0]
    cases=[('repeat', [((flags(hb,2),1000),)]*2),
           ('mixed', [((flags(direct[0],1),1000),),((flags(huff[0],0),1000),),((flags(jsc[-1],2),1000),)]),
           ('four',[tuple((flags(direct[i],1),hz) for i,hz in zip([0,3,4,10],[600,750,900,1050])),
                    tuple((flags(huff[i],2),hz) for i,hz in zip(range(4),[600,750,900,1050]))])]
    env=dict(os.environ,MINISHELL_ROOT=str(tmp),MINISHELL_APP_DIR=appdir)
    master,slave=pty.openpty();endpoint=os.ttyname(slave)
    commands=[];expected=[]
    for name,slots in cases:
        mono=b''.join(pcm(slot) for slot in slots)
        wavpath=flash/f'{name}.wav'
        # Each mono 12k sample becomes stereo L=R; same phase-0 samples as host.
        with wave.open(str(wavpath),'wb') as wav:
            wav.setparams((2,2,12000,0,'NONE','not compressed'))
            wav.writeframes(b''.join(mono[i:i+2]*2 for i in range(0,len(mono),2)))
        with wave.open(str(tmp/'mono.wav'),'wb') as wav:
            wav.setparams((1,2,12000,0,'NONE','not compressed'));wav.writeframes(mono)
        hostlog=tmp/f'{name}.host.jsonl'
        subprocess.run([decoder,'--all-slots','--messages','--log-jsonl',str(hostlog),
                        '--dial-hz','14078000','--start-utc','19700101T041000Z',str(tmp/'mono.wav')],
                       check=True,capture_output=True)
        expected.append((name,hostlog.read_text().splitlines(True)))
        commands.append(f'run js8_live_probe --rx /flash/{name}.wav --dial-hz 14078000 --cat serial:{endpoint} --log /flash/{name}.jsonl --slots {len(slots)}')
    # Reopen all three resource classes and append the same observations.
    commands.append(commands[0])
    traffic=bytearray();done=threading.Event()
    def drain():
        while not done.is_set():
            if select.select([master],[],[],0.02)[0]:
                traffic.extend(os.read(master,4096))
    reader=threading.Thread(target=drain);reader.start()
    try:
        result=subprocess.run([shell],input='\n'.join(commands+['exit','']),env=env,text=True,capture_output=True,timeout=40)
    finally:
        done.set();reader.join();os.close(master);os.close(slave)
    output=result.stdout+result.stderr
    assert result.returncode==0,output
    assert output.count('error=none')==4 and 'decode-busy-drop' not in output,output
    assert traffic==b'MD6;FR0;FT0;FA00014078000;'*4,traffic
    for name,want in expected:
        lines=(flash/f'{name}.jsonl').read_text().splitlines(True)
        if name=='repeat': want=want*2
        def normalize(match):
            key,value=match.groups()
            return f'"{key}":{int(value)-(15000 if key=="elapsed_s" else 1000)}'
        normalized=[re.sub(r'"(slot|elapsed_s|first_slot|last_slot)":(\d+)',normalize,line) for line in lines]
        assert normalized==want,(name,normalized,want,output)
    assert 'MESSAGE' in output and 'HELLOMSG ID 416' in output,output
    # Actual production target opens/cleans through public Audio on an empty WAV.
    with wave.open(str(flash/'empty.wav'),'wb') as wav:
        wav.setparams((2,2,12000,0,'NONE','not compressed'));wav.writeframes(bytes(4))
    for _ in range(2):
        result=subprocess.run([shell],input='run js8chat --rx /flash/empty.wav\n',env=env,text=True,capture_output=True,timeout=10)
        assert result.returncode==0 and result.stdout.count('error=none')==1,(result.stdout,result.stderr)
    print('\n'.join(line for line in output.splitlines() if 'JS8 decoded' in line or 'JS8 stopped' in line))
print('linux_js8_live: real loader/Audio/FS/PTY worker and exact host JSON parity: PASS')

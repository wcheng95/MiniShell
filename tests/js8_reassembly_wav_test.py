#!/usr/bin/env python3
"""Message host integration from accepted application bits (only TX flags vary)."""
from functools import lru_cache
import math
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile
import wave

exe, probe, root = sys.argv[1:]
root = Path(root)
env = os.environ.copy()
env.pop('JS8_JSC_DICT', None)
def vectors(name):
    return re.findall(r'"([01]{75})"', (root/'tests'/name).read_text())
directed = vectors('js8_directed_vectors.h')
huff = vectors('js8_huffman_vectors.h')
jsc = vectors('js8_jsc_vectors.h')
def flags(bits, value):
    return bits[:72] + format(value, '03b')
@lru_cache(None)
def samples(bits, hz):
    tones = subprocess.check_output([probe, '--tones', bits], text=True).strip()
    values, phase = [], 0.0
    for n in range(90000):
        sample = 0.0
        if 3000 <= n < 3000+79*960:
            sample = math.sin(phase)
            phase = (phase+2*math.pi*(hz+6.25*int(tones[(n-3000)//960]))/6000) % (2*math.pi)
        values.append(sample)
    return values
@lru_cache(None)
def slot(signals):
    streams = [samples(bits, hz) for bits, hz in signals]
    pcm = bytearray()
    for n in range(90000):
        value = round(0.2*32767*sum(s[n] for s in streams))
        assert -32768 <= value <= 32767
        pcm += struct.pack('<hh', value, value)
    return pcm

def signal(bits, tx=0, hz=1000):
    return ((flags(bits, tx), hz),)

with tempfile.TemporaryDirectory(prefix='js8-messages-') as temp:
    path = Path(temp)/'messages.wav'
    def run(sequence):
        with wave.open(str(path), 'wb') as wav:
            wav.setparams((1, 2, 12000, 0, 'NONE', 'not compressed'))
            wav.writeframes(b''.join(slot(signals) for signals in sequence))
        ordinary = subprocess.run([exe, '--all-slots', str(path)], env=env,
                                  capture_output=True, text=True, check=True)
        messages = subprocess.run([exe, '--all-slots', '--messages', str(path)], env=env,
                                  capture_output=True, text=True, check=True)
        lines = messages.stdout.splitlines(True)
        # Adding messages must retain every raw line and original diagnostic.
        assert ''.join(s for s in lines if not s.startswith('message ')) == ordinary.stdout
        assert ''.join(s for s in messages.stderr.splitlines(True)
                       if 'reassembly' not in s) == ordinary.stderr
        return [s.rstrip('\n') for s in lines if s.startswith('message ')], messages
    def expected(text, first=0, last=1, hz='1000.000', sender='AG6AQ', to='K1ABC'):
        return f'message from={sender} to={to} first_slot={first} last_slot={last} hz={hz} text="{text}"'
    hdr = signal(directed[0], 1)
    lines, _ = run([hdr, signal(huff[1], 2)])
    assert lines == [expected('HELLO WORLD')], lines
    lines, _ = run([hdr, signal(huff[0]), signal(jsc[-1], 2)])
    assert lines == [expected('HELLOMSG ID 416', last=2)], lines
    lines, _ = run([signal(directed[0], 3)])
    assert lines == [expected('', last=0)]
    lines, result = run([signal(huff[1], 2)])
    assert not lines and 'slot=0 reassembly=orphan\n' in result.stderr
    lines, result = run([hdr, (), signal(huff[1], 2)])
    assert not lines and 'slot=2 reassembly=gap\n' in result.stderr
    lines, _ = run([signal(directed[0], 1, 700), signal(huff[0], 0, 706.25),
                    signal(jsc[-1], 2, 696.875)])
    assert lines == [expected('HELLOMSG ID 416', last=2, hz='696.875')], lines
    lines, result = run([hdr, signal(huff[1], 2, 1012.5)])
    assert not lines and 'reassembly=orphan' in result.stderr
    # Four different accepted headers and four texts avoid T061's per-slot
    # exact-payload dedupe. Multiple audio offsets are present simultaneously.
    indices = [0, 3, 4, 10]
    frequencies = [600, 750, 900, 1050]
    starts = tuple((flags(directed[index], 1), hz) for index, hz in zip(indices, frequencies))
    ends = tuple((flags(huff[index], 2), hz) for index, hz in zip(range(4), frequencies))
    lines, _ = run([starts, ends])
    want = [expected('HELLO', hz='600.000'),
            expected('HELLO WORLD', hz='750.000', sender='AG6AQ/P'),
            expected('CQ FIELD', hz='900.000', to='K1ABC/P'),
            expected('ACK', hz='1050.000', to='@ALLCALL')]
    assert sorted(lines) == sorted(want), lines
    lines, result = run([hdr, signal(huff[0]), signal(directed[3], 1), signal(huff[1], 2)])
    assert lines == [expected('HELLO WORLD', first=2, last=3, sender='AG6AQ/P')]
    assert 'reassembly_drop expired=0x0 replaced=0x1 evicted=0x0' in result.stderr
    lines, result = run([hdr] + [()]*6 + [signal(huff[1], 2)])
    assert not lines and 'expired=0x1' in result.stderr and 'reassembly=orphan' in result.stderr
    # 31 x 34 spaces exceeds 1023; repeated identical raw fragments survive
    # per-slot dedupe and append once each, rather than being globally hidden.
    lines, result = run([hdr] + [signal(huff[8])]*30 + [signal(huff[8], 2)])
    assert not lines and 'slot=31 reassembly=overflow\n' in result.stderr
    assert len(result.stdout.splitlines()) == 32
    lines, _ = run([hdr, signal(huff[0]), signal(huff[0]), signal(huff[0], 2)])
    assert lines == [expected('HELLOHELLOHELLO', last=3)]
    for index in (1, 2, 6):
        lines, result = run([signal(directed[index], 3), signal(huff[1], 2)])
        assert not lines and 'reassembly=orphan' in result.stderr
    # Host escaping uses the JSC diagnostic rules for either codec's bytes.
    lines, _ = run([hdr, signal(huff[10]), signal(jsc[3], 2)])
    assert lines == [expected('\\"HELLO\\"\\n', last=2)], lines
    for args in (['--messages'], ['--messages', str(path)],
                 ['--messages', '--all-slots', str(path)], ['--all-slots', '--messages']):
        assert subprocess.run([exe]+args, capture_output=True).returncode == 2
print('js8_reassembly_wav_test: complete/mixed/four-stream/drift/gap/expiry/overflow/raw compatibility: PASS')

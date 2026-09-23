#!/usr/bin/env python3
"""Exact aligned host windows from accepted application bits; no reassembly."""
import math
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile

exe, probe, root = sys.argv[1:]
root = Path(root)
env = os.environ.copy()
env.pop('JS8_JSC_DICT', None)

def payload(name, index):
    return re.findall(r'"([01]{75})"', (root/'tests'/name).read_text())[index]

payloads = [payload('js8_compound_vectors.h', 0), payload('js8_directed_vectors.h', 1),
            payload('js8_huffman_vectors.h', 0), payload('js8_jsc_vectors.h', -1)]
suffixes = [' call=AG6AQ beacon="HB" grid=CM97', ' from=AG6AQ to=K1ABC cmd=" ACK" ack=1',
            ' codec=huffman data="HELLO"', ' codec=jsc data="MSG ID 416"']
slots = []
for bits in payloads:
    tones = subprocess.check_output([probe, '--tones', bits], text=True).strip()
    pcm, phase = bytearray(), 0.0
    for n in range(90000):
        value = 0
        if 3000 <= n < 3000+79*960:
            tone = int(tones[(n-3000)//960])
            value = round(0.5*32767*math.sin(phase))
            phase = (phase+2*math.pi*(1000+6.25*tone)/6000) % (2*math.pi)
        # Odd samples are hostile: only phase-0 even samples contain the signal.
        pcm += struct.pack('<hh', value, -32768)
    assert len(pcm) == 360000
    slots.append(pcm)
fmt = struct.pack('<HHIIHH', 1, 1, 12000, 24000, 2, 16)
def chunk(tag, data):
    return tag + struct.pack('<I', len(data)) + data + bytes(len(data)%2)
def riff(pcm, extra=b''):
    # Odd unknown chunk proves seeks use the data offset, not a fixed WAV header.
    body = b'WAVE' + chunk(b'JUNK', b'xyz') + chunk(b'fmt ', fmt) + chunk(b'data', pcm) + extra
    return b'RIFF' + struct.pack('<I', len(body)) + body
with tempfile.TemporaryDirectory(prefix='js8-multislot-') as temp:
    path = Path(temp)/'slots.wav'
    def run(pcm, all_slots=True, binary=exe, overrides=env):
        path.write_bytes(riff(pcm))
        return subprocess.run([binary] + (['--all-slots'] if all_slots else []) + [str(path)],
                              env=overrides, capture_output=True, text=True, check=True)
    def check(result, expected, count, trailing=0):
        lines = result.stdout.splitlines()
        assert len(lines) == len(expected), result.stdout
        for line, (slot, kind) in zip(lines, expected):
            assert line.startswith(f'slot={slot} slot_s={slot*15} payload={payloads[kind]} '), line
            assert line.endswith(suffixes[kind]), line
        summaries = re.findall(r'^slot=(\d+) blocks=93 ignored_engine_samples=720 .* unique=(\d+)$', result.stderr, re.M)
        assert len(summaries) == count, result.stderr
        for slot, (tag, unique) in enumerate(summaries):
            assert int(tag) == slot and int(unique) == sum(s == slot for s, _ in expected)
        for line in result.stderr.splitlines():
            if 'candidate=' in line:
                assert re.match(r'slot=\d+ candidate=', line), line
        assert f'slots={count} trailing_input_samples={trailing}\n' in result.stderr
    check(run(slots[0]+slots[1]), [(0,0), (1,1)], 2)
    check(run(slots[0]*2), [(0,0), (1,0)], 2)
    silent = bytes(360000)
    result = run(silent+slots[2], all_slots=False)
    assert result.stdout == '' and 'slot=' not in result.stderr
    check(run(silent+slots[2]), [(1,2)], 2)
    mixed = b''.join(slots)
    baseline = run(mixed)
    check(baseline, list(enumerate(range(4))), 4)
    # Strong tail values cannot shift boundaries or change candidates/decodes.
    tails = b''.join(s[:357120] + b'\xff\x7f'*1440 for s in slots)
    changed = run(tails)
    assert changed.stdout == baseline.stdout and changed.stderr == baseline.stderr
    # Almost a whole extra slot, containing a full signal, must still be ignored.
    check(run(slots[0]+slots[3][:359998]), [(0,0)], 1, 179999)
    # Same compressed payload in successive slots, with one allocation/open.
    reused = run(slots[3]*2, binary=probe)
    check(reused, [(0,3), (1,3)], 2)
    assert reused.stderr.endswith('probe workspace_allocations=1 dictionary_opens=1 stream_resets=2\n')
    # Later JSC error retains earlier non-JSC output, with deterministic failure.
    path.write_bytes(riff(slots[0]+slots[3]))
    missing = dict(env, JS8_JSC_DICT=str(Path(temp)/'missing.dict'))
    failed = subprocess.run([exe, '--all-slots', str(path)], env=missing, capture_output=True, text=True)
    assert failed.returncode == 1
    assert failed.stdout.splitlines()[0].endswith(suffixes[0])
    assert failed.stdout.splitlines()[1].endswith(' codec=jsc data_error=resource')
    assert failed.stderr.endswith('slots=2 trailing_input_samples=0\n')
    # Default mode remains byte-identical to the first slot's accepted window,
    # apart from its existing count of ignored samples on long input.
    first = run(slots[0], all_slots=False)
    default_long = run(mixed, all_slots=False)
    assert first.stdout == default_long.stdout and 'slot=' not in default_long.stderr
    assert 'ignored_engine_samples=270720 ' in default_long.stderr
    # Short input still works by default but is not a complete aligned slot.
    run(slots[0][:357120], all_slots=False)
    for data in (riff(slots[0][:357120]), riff(mixed)[:-1],
                 riff(mixed, b'JUNK'+struct.pack('<I', 1000)+b'x')):
        path.write_bytes(data)
        result = subprocess.run([exe, '--all-slots', str(path)], capture_output=True)
        assert result.returncode == 1 and result.stdout == b''
    for args in (['--all-slots'], ['--bad', str(path)], ['--all-slots', str(path), 'extra']):
        assert subprocess.run([exe]+args, capture_output=True).returncode == 2
print('js8_multislot_wav_test: geometry/dedupe/content/resource reuse/default/error contracts: PASS')

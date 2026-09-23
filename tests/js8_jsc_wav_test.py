#!/usr/bin/env python3
"""Real JSC1 integrity and host adapter/diagnostic regression. No upstream needed."""
import hashlib
import json
import math
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import wave

exe, fixture_exe, root, huff_exe = sys.argv[1:]
resource = Path(root) / 'apps/js8chat/resources/jsc.dict'
blob = resource.read_bytes()
assert len(blob) == 1918009
assert hashlib.sha256(blob).hexdigest() == 'ced6b30303f004966b29f7e658e7e60c8933526716b1b85c03384d0e9a417149'
assert struct.unpack_from('<4sHHIIIIII', blob) == (b'JSC1', 1, 256, 262144, 1024, 32, 4128, 1, 0)
# Verify all record lengths/offsets and the two full-string Tuple.size quirks.
pos, maximum = 4128, 0
for index in range(262144):
    if index % 256 == 0:
        assert struct.unpack_from('<I', blob, 32+index//256*4)[0] == pos
    size = blob[pos]
    maximum = max(maximum, size)
    value = blob[pos+1:pos+1+size]
    assert len(value) == size and b'\0' not in value
    if index == 81:
        assert value == b'@ALLCALL'
    if index == 262143:
        assert value == b'ROSIDS'
    pos += 1+size
assert pos == len(blob) and maximum == 26
fixtures = subprocess.check_output([fixture_exe, '--waveform-fixtures'], text=True).splitlines()
assert len(fixtures) == 21
clean_env = os.environ.copy()
clean_env.pop('JS8_JSC_DICT', None)
with tempfile.TemporaryDirectory(prefix='js8-jsc-') as temp:
    path = Path(temp) / 'test.wav'

    def make_wav(tones):
        pcm = bytearray()
        phase = 0.0
        for n in range(90000):
            sample = 0
            if 3000 <= n < 3000+79*960:
                tone = int(tones[(n-3000)//960])
                sample = round(0.5*32767*math.sin(phase))
                phase = (phase+2*math.pi*(1000+6.25*tone)/6000) % (2*math.pi)
            pcm += struct.pack('<hh', sample, sample)
        with wave.open(str(path), 'wb') as wav:
            wav.setparams((1, 2, 12000, 0, 'NONE', 'not compressed'))
            wav.writeframes(pcm)

    def run(env=clean_env):
        return subprocess.run([exe, str(path)], cwd=temp, env=env, capture_output=True)

    for fixture in fixtures:
        tones, payload, expected_hex = fixture.split('\t')
        make_wav(tones)
        result = run()
        assert result.returncode == 0, result.stderr
        lines = result.stdout.decode('latin-1').splitlines()
        assert len(lines) == 1 and lines[0].startswith('payload='+payload+' '), result.stdout
        assert ' class=data_compressed ' in lines[0]
        actual = json.loads(lines[0].split(' codec=jsc data=', 1)[1]).encode('latin-1')
        assert actual == bytes.fromhex(expected_hex), (actual, expected_hex)
        assert result.stderr.endswith(b'unique=1\n')
        if b'"' in actual or b'\\' in actual:
            assert b'\\"' in result.stdout or b'\\\\' in result.stdout
    assert actual == b'MSG ID 416'  # Last fixed vector is independently decoded A_2_1.
    # Override works independently of cwd, with exactly the same output.
    override = dict(clean_env, JS8_JSC_DICT=str(resource))
    assert run(override).stdout == result.stdout
    tones, _, _ = fixtures[0].split('\t')
    make_wav(tones)
    missing = dict(clean_env, JS8_JSC_DICT=str(Path(temp)/'missing.dict'))
    for env in (missing, dict(clean_env, JS8_JSC_DICT='')):
        failed = run(env)
        assert failed.returncode == 1
        assert b' codec=jsc data_error=resource\n' in failed.stdout and b' data="' not in failed.stdout
    corrupt = Path(temp)/'corrupt.dict'
    broken = []
    magic = bytearray(blob); magic[0] = 0; broken.append(magic)
    offset = bytearray(blob); struct.pack_into('<I', offset, 32, len(blob)); broken.append(offset)
    length = bytearray(blob); length[4128] = 27; broken.append(length)
    broken.append(blob[:-1])
    for data in broken:
        corrupt.write_bytes(data)
        failed = run(dict(clean_env, JS8_JSC_DICT=str(corrupt)))
        assert failed.returncode == 1 and b'codec=jsc data_error=resource' in failed.stdout
    # Non-JSC remains usable despite absent or corrupt dictionary.
    huff = subprocess.check_output([huff_exe, '--waveform-fixtures'], text=True).splitlines()[0]
    make_wav(huff.split('\t')[0])
    for env in (missing, dict(clean_env, JS8_JSC_DICT=str(corrupt))):
        result = run(env)
        assert result.returncode == 0 and result.stdout.endswith(b' codec=huffman data="HELLO"\n')
print('js8_jsc_wav_test: resource hash/structure, 21 vectors, adapters/errors: PASS')

#!/usr/bin/env python3
"""Huffman host diagnostics using fixed independent payload/content fixtures.

The unit fixture executable applies the existing PHY encoder only to make audio;
expected application content comes from the source-pinned vector header.
"""
import json
import math
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import wave

fixtures = subprocess.check_output([sys.argv[2], '--waveform-fixtures'], text=True).splitlines()
assert len(fixtures) == 13
with tempfile.TemporaryDirectory(prefix='js8-huffman-') as temp:
    path = Path(temp) / 'huffman.wav'
    for fixture in fixtures:
        tones, payload, fragment = fixture.split('\t')
        assert len(tones) == 79 and len(payload) == 75
        pcm = bytearray()
        phase = 0.0
        for n in range(90000):
            sample = 0
            if 3000 <= n < 3000 + 79 * 960:
                tone = int(tones[(n-3000)//960])
                sample = round(0.5 * 32767 * math.sin(phase))
                phase = (phase + 2*math.pi*(1000+6.25*tone)/6000) % (2*math.pi)
            pcm += struct.pack('<hh', sample, sample)
        with wave.open(str(path), 'wb') as wav:
            wav.setparams((1, 2, 12000, 0, 'NONE', 'not compressed'))
            wav.writeframes(pcm)
        result = subprocess.run([sys.argv[1], str(path)], text=True, capture_output=True, check=True)
        lines = result.stdout.splitlines()
        assert len(lines) == 1 and lines[0].startswith('payload=' + payload + ' '), result.stdout
        assert ' class=data tx=' in lines[0], result.stdout
        assert lines[0].endswith(" codec=huffman data=" + json.dumps(fragment)), result.stdout
        assert result.stderr.endswith('unique=1\n'), result.stderr
print('js8_huffman_wav_test: PASS')

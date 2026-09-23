#!/usr/bin/env python3
"""Host WAV contract/decimation regression, with no external fixture."""
import math
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile

exe = sys.argv[1]
root = Path(__file__).resolve().parent
# Extract the third checked-in upstream vector, not production channel output.
vectors = (root / 'js8_golden_vectors.h').read_text()
payload = re.findall(r'"([01]{75})"', vectors)[2]
tones = [int(x) for x in re.findall(r'\d+', re.findall(r'\{([0-7, ]+)\}', vectors)[2])]
assert len(tones) == 79
pcm = bytearray()
phase = 0.0
for n in range(90000):
    sample = 0
    if 3000 <= n < 3000 + 79 * 960:
        tone = tones[(n - 3000) // 960]
        sample = round(0.5 * 32767 * math.sin(phase))
        phase = (phase + 2 * math.pi * (1000 + 6.25 * tone) / 6000) % (2 * math.pi)
    # Only even samples carry the signal: verifies phase-0 continuous decimation.
    pcm += struct.pack('<hh', sample, -32768)
fmt = struct.pack('<HHIIHH', 1, 1, 12000, 24000, 2, 16)


def chunk(tag, data):
    return tag + struct.pack('<I', len(data)) + data + b'\0' * (len(data) % 2)


def riff(*chunks):
    body = b'WAVE' + b''.join(chunks)
    return b'RIFF' + struct.pack('<I', len(body)) + body


with tempfile.TemporaryDirectory(prefix='js8-wav-test-') as temp:
    path = Path(temp) / 'test.wav'

    def run(data, success=True, decoded=False):
        path.write_bytes(data)
        result = subprocess.run([exe, str(path)], capture_output=True, text=True)
        assert (result.returncode == 0) == success, (result.returncode, result.stderr)
        if decoded:
            lines = result.stdout.splitlines()
            assert len(lines) == 1, result.stdout
            assert lines[0].startswith(f'payload={payload} type=3 frame="CVUJtH2w2sAS" tx_raw=3 class=compound tx=FIRST|LAST '), result.stdout
            assert 'blocks=93 ignored_engine_samples=720 ' in result.stderr, result.stderr
            assert result.stderr.endswith('unique=1\n'), result.stderr
        return result

    standard = riff(chunk(b'fmt ', fmt), chunk(b'data', pcm))
    run(standard, decoded=True)
    # Unknown odd chunk, odd-length extended fmt, and data before fmt.
    run(riff(chunk(b'JUNK', b'abc'), chunk(b'data', pcm), chunk(b'fmt ', fmt + b'\0')), decoded=True)
    # Pinned WAV convention: final LIST pad exists, but RIFF size excludes it.
    padded = bytearray(riff(chunk(b'fmt ', fmt), chunk(b'data', pcm), chunk(b'LIST', b'abc')))
    struct.pack_into('<I', padded, 4, len(padded) - 9)
    run(padded, decoded=True)
    # Changing only the ignored 720-sample tail must not alter any decode.
    changed = pcm[:-2880] + b'\xff' * 2880
    run(riff(chunk(b'fmt ', fmt), chunk(b'data', changed)), decoded=True)
    odd = run(riff(chunk(b'fmt ', fmt), chunk(b'data', bytes(1919 * 2))))
    assert 'blocks=1 ignored_engine_samples=0 ' in odd.stderr

    for bad_fmt in (
        struct.pack('<HHIIHH', 3, 1, 12000, 24000, 2, 16),
        struct.pack('<HHIIHH', 1, 2, 12000, 48000, 4, 16),
        struct.pack('<HHIIHH', 1, 1, 6000, 12000, 2, 16),
        struct.pack('<HHIIHH', 1, 1, 12000, 12000, 1, 8),
        struct.pack('<HHIIHH', 1, 1, 12000, 24001, 2, 16),
        struct.pack('<HHIIHH', 1, 1, 12000, 24000, 3, 16),
        fmt[:15],
    ):
        run(riff(chunk(b'fmt ', bad_fmt), chunk(b'data', pcm)), success=False)
    for bad in (
        b'', b'not a WAV', standard[:11], standard[:-1],
        b'RIFX' + standard[4:], standard[:8] + b'NOTW' + standard[12:],
        riff(chunk(b'fmt ', fmt)), riff(chunk(b'data', pcm)),
        riff(chunk(b'fmt ', fmt), chunk(b'fmt ', fmt), chunk(b'data', pcm)),
        riff(chunk(b'fmt ', fmt), chunk(b'data', pcm), chunk(b'data', b'')),
        riff(chunk(b'fmt ', fmt), chunk(b'data', b'1')),
        riff(chunk(b'fmt ', fmt), chunk(b'data', bytes(100))),
        riff(chunk(b'fmt ', fmt), chunk(b'data', bytes(94 * 960 * 4))),
        riff(chunk(b'fmt ', fmt), b'JUNK' + struct.pack('<I', 0xffffffff)),
        riff(chunk(b'fmt ', fmt), b'data' + struct.pack('<I', 360000) + bytes(100)),
        bytes(padded[:-1]),
    ):
        run(bad, success=False)
    assert subprocess.run([exe], capture_output=True).returncode == 2
    assert subprocess.run([exe, str(path) + '.missing'], capture_output=True).returncode == 1
print('js8_wav_test: PASS')

#!/usr/bin/env python3
"""Optional external v3.0.3 A_2_1 regression; fixture is never copied into repo."""
import hashlib
import os
from pathlib import Path
import re
import subprocess
import sys

wav = Path(sys.argv[2]).read_bytes()
assert len(wav) == 360208
assert hashlib.sha1(b'blob ' + str(len(wav)).encode() + b'\0' + wav).hexdigest() == (
    'd986a4e5a9cc654dffbfadae73ec35cc9cea1d83')
env = os.environ.copy()
env.pop('JS8_JSC_DICT', None)
result = subprocess.run([sys.argv[1], sys.argv[2]], capture_output=True, text=True, check=True, env=env)
expected = ('payload=111001011101001010000111001011100000101011000001100010000111111111111111010 '
            'type=2 frame="vTA7BWh1Y7++" tx_raw=2 class=data_compressed tx=LAST')
lines = result.stdout.splitlines()
assert len(lines) == 1 and lines[0].startswith(expected + ' '), result.stdout
assert lines[0].endswith(' codec=jsc data="MSG ID 416"'), result.stdout
assert re.search(r'blocks=93 ignored_engine_samples=720 .* unique=1\n', result.stderr), result.stderr
print(result.stdout, end='')
print(result.stderr, end='')
print('js8_wav_reference: PASS')

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
# T061 locks the complete accepted T060 default output, including diagnostics.
expected_stdout = expected + ' score=26 time=5/0 freq=57/0 hz=556.250 hard_errors=15 codec=jsc data="MSG ID 416"\n'
expected_stderr = (
    'candidate=0 score=26 time=5/0 freq=57/0 status=0\n'
    'candidate=1 score=14 time=5/0 freq=57/1 status=-2\n'
    'candidate=2 score=12 time=18/0 freq=54/0 status=-2\n'
    'candidate=3 score=10 time=5/0 freq=56/1 status=-2\n'
    'candidate=4 score=10 time=17/0 freq=110/1 status=-2\n'
    'blocks=93 ignored_engine_samples=720 candidates=50 ldpc_fail=49 crc_fail=0 valid=1 unique=1\n')
assert result.stdout == expected_stdout and result.stderr == expected_stderr, (result.stdout, result.stderr)
all_slots = subprocess.run([sys.argv[1], '--all-slots', sys.argv[2]], capture_output=True,
                           text=True, check=True, env=env)
assert all_slots.stdout == 'slot=0 slot_s=0 ' + expected_stdout, all_slots.stdout
assert all_slots.stderr == ''.join('slot=0 ' + line for line in expected_stderr.splitlines(True)) + 'slots=1 trailing_input_samples=0\n', all_slots.stderr
print(result.stdout, end='')
print(result.stderr, end='')
print('js8_wav_reference: PASS')

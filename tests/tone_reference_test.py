#!/usr/bin/env python3
"""Verify the copied renderer against normalized pinned-source fingerprints."""
import hashlib
import json
import pathlib
import re
import sys
root = pathlib.Path(sys.argv[1]).resolve()
source = (root / 'platform/common/tone_stream.c').read_text()
expected = json.loads((root / 'tests/tone_reference.json').read_text())


def normalize(text):
    text = re.sub(r'/\*.*?\*/|//[^\n]*', '', text, flags=re.S)
    text = text.replace('portENTER_CRITICAL(&s_busy_mux)', 's_port.busy_enter()')
    text = text.replace('portEXIT_CRITICAL(&s_busy_mux)', 's_port.busy_exit()')
    text = text.replace('portMAX_DELAY', 'UINT32_MAX')
    return re.sub(r'\s+', '', text)


def function(text, name):
    match = re.search(r'^static [^;{]+\b' + name + r'\([^;]*?\)\n\{', text, re.M)
    assert match, name
    start = match.start()
    pos = match.end()
    depth = 1
    while depth:
        if text[pos] == '{': depth += 1
        if text[pos] == '}': depth -= 1
        pos += 1
    return text[start:pos]


def digest(text):
    return hashlib.sha256(normalize(text).encode()).hexdigest()


for name, fingerprint in expected['functions'].items():
    assert digest(function(source, name)) == fingerprint, name
sample_loop = source[source.index('uint32_t keyed_tally = 0;'):source.index('    commit->samples = keyed_tally;')]
assert digest(sample_loop) == expected['sample_loop'], 'per-sample render loop drifted'
print(f"Mini-CW {expected['commit']}: {len(expected['functions'])} functions + render loop match pinned source")

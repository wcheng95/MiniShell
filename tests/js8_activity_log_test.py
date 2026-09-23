#!/usr/bin/env python3
"""Strict ASCII JSON, binary-safe strings and append preservation."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
with tempfile.TemporaryDirectory(prefix='js8-json-') as temp:
    path = Path(temp)/'activity.jsonl'
    prefix = b'{"existing":true}\n'
    path.write_bytes(prefix)
    for _ in range(2):
        subprocess.run([sys.argv[1], str(path)], check=True)
    raw = path.read_bytes()
    assert raw.startswith(prefix) and raw.isascii()
    assert len(raw.splitlines()) == 3
    records = [json.loads(line) for line in raw.splitlines()[1:]]
    assert records[0] == records[1]
    event = records[0]
    assert event['schema'] == 'js8-activity-v1' and event['event'] == 'MESSAGE'
    assert event['from'] == 'A"\\' and event['audio_millihz'] == -3125
    assert event['text'].encode('latin1') == bytes(i % 256 for i in range(1023))
    assert not {'utc', 'dial_hz', 'rf_millihz'} & event.keys()
    assert b'\\u0000' in raw and b'\\u000a' in raw and b'\\u0009' in raw
    assert b'\\u007f' in raw and b'\\u0080' in raw and b'\\u00ff' in raw
print('js8_activity_log_test: all Latin-1 bytes round trip and append: PASS')

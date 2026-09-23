#!/usr/bin/env python3
"""Receive-only CAT and no old-engine edits are separately reviewed invariants."""
from pathlib import Path
import re
import sys
root=Path(sys.argv[1])/'apps/js8chat'
for directory in ('main','src/live_rx','src/activity_json'):
    for path in (root/directory).glob('*.[ch]'):
        source=path.read_text()
        assert not re.search(r'"(?:TX;|RX;|TA|TM)',source),path
        assert not re.search(r'#\s*include.*(?:ft8|pthread|alsa|termios|unistd|platform)',source),path
        assert not re.search(r'\b(?:fopen|pthread_create|snd_pcm_open|clock_gettime)\s*\(',source),path
cat=(root/'src/live_rx/js8_qmx.c').read_text()
assert all(s in cat for s in ('"MD6;"','"FR0;"','"FT0;"','"FA%011u;"'))
print('js8_live_boundary: public services, no FT8 linkage/includes, receive-only CAT: PASS')

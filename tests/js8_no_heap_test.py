#!/usr/bin/env python3
"""Check compiled JS8 libraries, including FFT, for hidden heap dependencies."""
import re
import subprocess
import sys

for archive in sys.argv[2:]:
    symbols = subprocess.check_output([sys.argv[1], '-u', archive], text=True)
    assert not re.search(r'\b(?:malloc|calloc|realloc|aligned_alloc|free|strdup|asprintf)\b', symbols), symbols
print('js8_no_heap_test: PASS')

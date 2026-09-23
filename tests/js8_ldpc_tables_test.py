#!/usr/bin/env python3
"""Check pinned table integrity, graph reciprocity and H * G^T == 0."""
import hashlib
from pathlib import Path
import re
import sys


def table(text, name):
    body = text.split(name, 1)[1].split('=', 1)[1].split('};', 1)[0] + '}'
    return [[int(n, 0) for n in re.findall(r'0x[0-9a-f]+|\b\d+\b', row)]
            for row in re.findall(r'\{([^{}]+)\}', body)]


def main():
    source = Path(sys.argv[1]) / 'apps/js8chat/src/js8_engine/js8_ldpc_tables.h'
    text = source.read_text()
    generator = table(text, 'kGenerator')
    reverse = table(text, 'kBitChecks')
    lengths, = table(text, 'kCheckLengths')
    checks = table(text, 'kCheckBits')
    assert len(generator) == len(lengths) == len(checks) == 87
    assert len(reverse) == 174
    assert all(len(row) == 11 and not row[-1] & 1 for row in generator)
    assert all(len(row) == 3 and len(set(row)) == 3 for row in reverse)
    assert all(len(row) == 7 for row in checks)
    assert max(lengths) == 7 and sum(lengths) == 174 * 3
    raw = bytes(n for group in (generator, reverse, [lengths], checks)
                for row in group for n in row)
    assert hashlib.sha256(raw).hexdigest() == 'f7a5608687c1a782bfabde44ebe7ae01d3355c631fc79ad99bc3c86405bd00c9'
    derived = [[] for _ in range(174)]
    for check, (row, count) in enumerate(zip(checks, lengths)):
        assert 0 < count <= 7 and len(set(row[:count])) == count
        assert all(value == 0 for value in row[count:])
        for bit in row[:count]:
            assert 0 <= bit < 174
            derived[bit].append(check)
    assert derived == reverse
    # Every one-hot information word must satisfy every parity check.
    # This validates all 87 columns, beyond the three fixed payload vectors.
    for col in range(87):
        cw = [(row[col // 8] >> (7 - col % 8)) & 1 for row in generator]
        cw += [int(i == col) for i in range(87)]
        for row, count in zip(checks, lengths):
            assert sum(cw[bit] for bit in row[:count]) % 2 == 0
    print('js8_ldpc_tables_test: PASS')


if __name__ == '__main__':
    main()

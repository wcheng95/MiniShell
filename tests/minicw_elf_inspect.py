#!/usr/bin/env python3
"""Inspect the stripped T042 ELF without relying on its removed .dynamic section."""
import pathlib
import struct
import sys


def inspect(path):
    data = pathlib.Path(path).read_bytes()
    assert data[:6] == b'\x7fELF\x01\x01', 'expected ELF32 little endian'
    header = struct.unpack_from('<HHIIIIIHHHHHH', data, 16)
    kind, machine, _, entry, _, shoff, _, _, _, _, shentsize, shnum, shstrndx = header
    assert kind == 3 and machine == 94, 'expected Xtensa shared ELF'
    sections = [struct.unpack_from('<10I', data, shoff + i * shentsize) for i in range(shnum)]

    def payload(sec):
        return data[sec[4]:sec[4] + sec[5]]

    def string(blob, offset):
        return blob[offset:blob.index(b'\0', offset)].decode('ascii')

    names = payload(sections[shstrndx])
    named = {string(names, sec[0]): sec for sec in sections}
    symbols = named['.dynsym']
    strings = payload(sections[symbols[6]])
    imports = set()
    for offset in range(symbols[4], symbols[4] + symbols[5], symbols[9]):
        name, _, _, _, _, index = struct.unpack_from('<IIIBBH', data, offset)
        if name and index == 0:
            imports.add(string(strings, name))
    assert imports == {'mini_api_get'}, imports
    loaded_names = tuple(n for n in ('.text', '.data', '.rodata', '.data.rel.ro', '.bss') if n in named)
    loaded = [named[name] for name in loaded_names]
    for name in ('.data', '.rodata', '.data.rel.ro'):
        assert name not in named or named[name][5] % 4 == 0, f'{name}: loader packing loses alignment'
    assert named['.text'][3] <= entry < named['.text'][3] + named['.text'][5]
    relocations = 0
    for sec in sections:
        if sec[1] != 4:  # SHT_RELA
            continue
        for offset in range(sec[4], sec[4] + sec[5], sec[9]):
            target, info, _ = struct.unpack_from('<IIi', data, offset)
            if info & 255 == 0:
                continue
            assert any(s[3] <= target and target + 4 <= s[3] + s[5] for s in loaded), hex(target)
            relocations += 1
    text_bytes = (named['.text'][5] + 3) & ~3
    data_bytes = sum(named[n][5] for n in loaded_names[1:])
    print(f'PASS: sole resident import mini_api_get; {relocations} mapped relocations')
    print(f'ELF {len(data)} bytes; section-loader allocations: text {text_bytes}, data {data_bytes}, total {text_bytes + data_bytes}')
    for name in loaded_names:
        print(f'{name}: {named[name][5]} bytes')


if __name__ == '__main__':
    inspect(sys.argv[1])

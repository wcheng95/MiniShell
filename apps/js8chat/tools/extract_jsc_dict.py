#!/usr/bin/env python3
"""Extract the JS8Call v3.0.3 JSC receive dictionary into MCU-friendly JSC1 format."""

from __future__ import annotations

import argparse
import ast
import hashlib
import json
import re
import struct
import sys
import urllib.request
from pathlib import Path

UPSTREAM_REPOSITORY = "JS8Call-improved/JS8Call-improved"
UPSTREAM_REF = "v3.0.3"
DEFAULT_SOURCE = (
    "https://raw.githubusercontent.com/JS8Call-improved/JS8Call-improved/"
    f"{UPSTREAM_REF}/JS8_JSC/JSC_map.cpp"
)
EXPECTED_ENTRIES = 262_144
BLOCK_SIZE = 256
MAGIC = b"JSC1"
VERSION = 1
FLAG_LATIN1 = 1
HEADER = struct.Struct("<4sHHIIIIII")
U32 = struct.Struct("<I")

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUTPUT = ROOT / "resources" / "jsc.dict"
DEFAULT_META = ROOT / "resources" / "jsc.dict.meta.json"

ENTRY_RE = re.compile(
    r'\{\s*"((?:\\.|[^"\\])*)"\s*(?:/\*.*?\*/\s*)*,\s*(\d+)\s*,\s*(\d+)\s*\}'
)

def read_source(source: str) -> str:
    if source.startswith(("http://", "https://")):
        with urllib.request.urlopen(source, timeout=60) as response:
            return response.read().decode("utf-8")
    return Path(source).read_text(encoding="utf-8")

def decode_cpp_string(raw: str) -> bytes:
    value = ast.literal_eval('"' + raw + '"')
    try:
        return value.encode("latin-1")
    except UnicodeEncodeError as exc:
        raise ValueError(f"JSC entry is not Latin-1: {value!r}") from exc

def extract_entries(text: str) -> tuple[list[bytes], list[dict[str, int]]]:
    marker = "const Tuple JSC::map[262144]"
    pos = text.find(marker)
    if pos < 0:
        raise ValueError("could not find JSC::map[262144] declaration")

    entries: list[bytes] = []
    mismatches: list[dict[str, int]] = []
    for match in ENTRY_RE.finditer(text, pos):
        raw, declared_text, index_text = match.groups()
        declared = int(declared_text)
        index = int(index_text)
        if index != len(entries):
            raise ValueError(f"non-sequential dictionary index: got {index}, expected {len(entries)}")
        word = decode_cpp_string(raw)
        if len(word) != declared:
            mismatches.append({"index": index, "declared_length": declared, "actual_length": len(word)})
        if len(word) > 255:
            raise ValueError(f"entry {index} is too long for JSC1: {len(word)}")
        entries.append(word)
        if len(entries) == EXPECTED_ENTRIES:
            break
    if len(entries) != EXPECTED_ENTRIES:
        raise ValueError(f"expected {EXPECTED_ENTRIES} entries, extracted {len(entries)}")
    return entries, mismatches

def build_jsc1(entries: list[bytes]) -> bytes:
    block_count = (len(entries) + BLOCK_SIZE - 1) // BLOCK_SIZE
    index_offset = HEADER.size
    data_offset = index_offset + block_count * U32.size
    payload = bytearray()
    block_offsets: list[int] = []
    for index, word in enumerate(entries):
        if index % BLOCK_SIZE == 0:
            block_offsets.append(data_offset + len(payload))
        payload.append(len(word))
        payload.extend(word)
    header = HEADER.pack(MAGIC, VERSION, BLOCK_SIZE, len(entries), block_count,
                         index_offset, data_offset, FLAG_LATIN1, 0)
    out = bytearray(header)
    for offset in block_offsets:
        out.extend(U32.pack(offset))
    out.extend(payload)
    return bytes(out)

def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", default=DEFAULT_SOURCE, help="JSC_map.cpp path or URL")
    parser.add_argument("--output", default=str(DEFAULT_OUTPUT))
    parser.add_argument("--meta", default=str(DEFAULT_META))
    args = parser.parse_args()

    entries, mismatches = extract_entries(read_source(args.source))
    blob = build_jsc1(entries)

    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(blob)

    digest = hashlib.sha256(blob).hexdigest()
    meta = {
        "format": "JSC1",
        "format_version": VERSION,
        "source": args.source,
        "upstream_repository": UPSTREAM_REPOSITORY,
        "upstream_ref": UPSTREAM_REF,
        "entry_count": len(entries),
        "block_size": BLOCK_SIZE,
        "block_count": (len(entries) + BLOCK_SIZE - 1) // BLOCK_SIZE,
        "header_bytes": HEADER.size,
        "block_index_bytes": ((len(entries) + BLOCK_SIZE - 1) // BLOCK_SIZE) * U32.size,
        "string_bytes": sum(map(len, entries)),
        "max_entry_bytes": max(map(len, entries)),
        "output_bytes": len(blob),
        "sha256": digest,
        "encoding": "latin-1",
        "upstream_license": "GPL-3.0-or-later",
        "source_length_mismatch_count": len(mismatches),
        "source_length_mismatches": mismatches,
    }

    meta_path = Path(args.meta)
    meta_path.parent.mkdir(parents=True, exist_ok=True)
    meta_path.write_text(json.dumps(meta, indent=2, sort_keys=True) + "\n")
    print(f"entries:      {len(entries)}")
    print(f"string bytes: {sum(map(len, entries))}")
    print(f"output bytes: {len(blob)}")
    print(f"max entry:    {max(map(len, entries))}")
    print(f"length quirks:{len(mismatches):>7}")
    print(f"sha256:       {digest}")
    return 0

if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        raise

# JSC Dictionary for MCU TX/RX

## Why this exists

Original JS8Call already contains a 262,144-entry JSC `(s,c)`-Dense dictionary. The upstream implementation emits two very large generated C++ tables (`map[]` and `list[]`). Those generated source files are convenient for desktop compilation but are a poor storage format for an MCU with limited internal flash.

For **decompression**, the upstream algorithm computes a dictionary index `j` directly from the compressed codeword and then performs only:

```text
j -> map[j].str
```

For **compression**, upstream uses its ordered `prefix[]` / `list[]` lookup structure and the per-entry `Tuple.size` value to find the first matching input prefix and determine how many characters were consumed.

The MiniShell JS8Chat v0.1 app will preserve these upstream JSC lookup/compression semantics rather than introducing a different hash-table or trie algorithm.

## Storage decision

JSC TX/RX is part of v0.1.

The resource does **not** have to live on microSD. The current size measurements indicate that a compact TX+RX representation should be small enough to consider storing directly in Cardputer ADV's 8 MB internal flash or compiling it into the firmware image. microSD remains an optional alternative.

Preferred implementation choices, in order of simplicity after the final size is known:

```text
1. dedicated read-only internal-flash partition
2. compiled-in read-only binary resource
3. microSD external resource
```

A dedicated flash partition has the advantage that the dictionary can be updated independently of the application image while consuming essentially no runtime RAM.

Do not freeze the final flash layout until the upstream TX lookup structures have also been packed and measured.

## Actual packed RX size

Extraction of the complete 262,144-entry upstream `map[]` table produced:

```text
raw string bytes        1,651,737
per-entry length bytes    262,144
block index                 4,096
header                         32
--------------------------------
JSC1 RX file            1,918,009 bytes
                        ~1.83 MiB
```

Maximum dictionary entry string length is only 26 bytes.

This is dramatically smaller than the roughly 6-7 MB generated C++ source file.

Current generated-file SHA-256:

```text
ced6b30303f004966b29f7e658e7e60c8933526716b1b85c03384d0e9a417149
```

## Upstream source

The MiniShell extractor is pinned directly to the frozen JS8Call-improved v3.0.3 source:

```text
repository: JS8Call-improved/JS8Call-improved
release:    v3.0.3
file:       JS8_JSC/JSC_map.cpp
entries:    262144
```

The complete v3.0.3 map was compared entry-by-entry with the historical map used during early JS8Chat research. All 262,144 strings and all declared `Tuple.size` values are identical, including the two known size quirks. Therefore the existing JSC1 binary remains valid for the frozen v3.0.3 target and retains SHA-256 `ced6b30303f004966b29f7e658e7e60c8933526716b1b85c03384d0e9a417149`.

The generated dictionary remains derived from upstream GPL-licensed material. MiniShell/JS8Chat is open source, so the project model is compatible with preserving the upstream GPL requirements and attribution.

## Binary RX format: JSC1

All multibyte integers are little-endian.

### 32-byte header

```text
offset  size  field
0       4     magic = "JSC1"
4       2     format version = 1
6       2     block size = 256 entries
8       4     entry count = 262144
12      4     block count = 1024
16      4     block-index offset = 32
20      4     string-data offset
24      4     flags (bit 0 = Latin-1 strings)
28      4     reserved = 0
```

### Block index

There are 1024 little-endian `uint32_t` absolute file offsets, one for every 256 dictionary entries.

Size:

```text
1024 * 4 = 4096 bytes
```

The whole index may be cached in RAM if convenient, but it does **not** have to be. An MCU can read a single 4-byte block offset directly from flash or SD.

### String data

Entries remain in dictionary-index order. Each RX entry is:

```text
uint8_t length
uint8_t bytes[length]     // Latin-1
```

No per-entry dictionary index is stored because the index is implicit from position.

## Upstream `Tuple.size` quirks

The upstream generated table contains at least two entries whose `Tuple.size` field does not equal the actual NUL-terminated string length:

```text
index 81      "@ALLCALL"   declared 7, actual 8
index 262143  "ROSIDS"     declared 1, actual 6
```

This does not affect RX because upstream decompression returns the full `map[j].str`, but it **does matter to TX**, because `Tuple.size` controls how much input text the compressor consumes after a match.

Therefore the TX resource must preserve the upstream declared consume length independently of the actual string length.

## RX lookup algorithm

To decode dictionary index `j`:

```text
block = j / 256
slot  = j % 256

read uint32 block_offset from:
    32 + block * 4

seek block_offset
scan slot length-prefixed records
return selected string
```

At worst this scans 255 short strings. That cost is insignificant compared with JS8 RF frame time, and a small flash/SD page cache can remove most repeated I/O.

## TX lookup strategy

v0.1 follows upstream JS8Call's JSC design:

```text
input text
   -> prefix table
   -> ordered candidate range in list[]
   -> first matching prefix
   -> JSC dictionary index
   -> upstream (s,c)-Dense codeword generation
```

The MCU representation does not need to duplicate the upstream C++ structs byte-for-byte. It may pack the same ordered lookup information into a compact read-only binary resource, but the resulting lookup decision must match upstream semantics.

The same dictionary strings are shared by TX and RX; no second copy of the 1.65 MiB string corpus is required. TX additionally needs compact ordering/index information and the declared consume length for each relevant entry.

The current rough expectation for complete TX+RX JSC storage is only a few MiB (approximately 2.5-3 MiB), but that remains an estimate until the TX structures are actually extracted and packed.

## Codec selection on TX

JS8Chat v0.1 supports both codecs:

```text
TX: Huffman + JSC
RX: Huffman + JSC
```

For each outgoing data frame:

```text
build Huffman candidate
build JSC candidate
choose whichever consumes more source characters
```

This matches upstream behavior and avoids sacrificing airtime merely to simplify the MCU implementation.

## Resource failure policy

Regardless of whether the JSC resource resides in internal flash or on SD, it should be versioned and validated before use.

If the JSC resource is unavailable or invalid:

```text
Huffman TX/RX remains available
JSC TX disabled
JSC RX frames cannot be decoded
UI clearly indicates reduced interoperability
```

The normal ADV build is expected to include the JSC resource, so this is a fault/fallback mode rather than the intended operating configuration.

## Reproducibility

MiniShell now owns the generator and generated RX resource:

```text
apps/js8chat/tools/extract_jsc_dict.py
apps/js8chat/resources/jsc.dict
apps/js8chat/resources/jsc.dict.meta.json
```

The generator is pinned to JS8Call-improved `v3.0.3`. The workflow:

```text
.github/workflows/js8-jsc.yml
```

regenerates the resource and verifies that the checked-in binary/metadata exactly match the frozen upstream reference.

The next JSC implementation step is TX support: preserve/generate the upstream `prefix[]` / `list[]` ordering and declared consume lengths in a compact resource while keeping behavior equivalent to the v3.0.3 `JSC::lookup()` / `JSC::compress()` implementation.

# T060 independent JSC RX oracle and resource evidence

Normative repository: `JS8Call-improved/JS8Call-improved`, tag **v3.0.3**.
The decoder/table derive from Jordan Sherer's upstream GPL-3.0-or-later JSC code.

| Pinned file | SHA-256 |
| --- | --- |
| [JS8_JSC/JSC.cpp](https://github.com/JS8Call-improved/JS8Call-improved/blob/v3.0.3/JS8_JSC/JSC.cpp) | `0f1c974a96fd65e043b1a4dbdb69a9ae43e42dafe81f5f22f196b09596dbcdeb` |
| [JS8_JSC/JSC.h](https://github.com/JS8Call-improved/JS8Call-improved/blob/v3.0.3/JS8_JSC/JSC.h) | `3edfda65865dc4ede66730113c1c7250861253d78ed83f17626ce55c79b0f1d8` |
| [JS8_JSC/JSC_map.cpp](https://github.com/JS8Call-improved/JS8Call-improved/blob/v3.0.3/JS8_JSC/JSC_map.cpp) | `ab2bd62ef594f4629a2c93b6de43f5469b1fd6fe67ebf4d24f915bd11ccef813` |

Offline regeneration:

```sh
python3 tests/js8_jsc_oracle.py /path/to/JSC.cpp /path/to/JSC.h /path/to/JSC_map.cpp > tests/js8_jsc_vectors.h
```

The oracle SHA-checks all three files, independently parses all map strings as
Latin-1 (ignoring Tuple.size), and transcribes upstream's two-pass nibble/separator
and dense-index decode. It never calls MiniShell production code or the resource
extractor. Fixture bitstreams use `JSC::codeword` arithmetic only offline, without
compression or prefix/list lookup. Normal tests use the small fixed vector header;
no upstream checkout or resource regeneration is needed.

Before implementing the C decoder, the oracle derived the accepted A_2_1 payload's
text as **`MSG ID 416`**, Latin-1 hex **`4d534720494420343136`**. Its unpadded stream
has 54 bits and six dictionary words; the exact payload is the final vector.

The 21 vectors cover indices 0, 6, 7, 69, 70, 81, 637, 10000, 262143; full
`@ALLCALL` and `ROSIDS`; spaces; a terminal without a separator; partial nibble;
unterminated prefix; out-of-range index; an overlong continuation sequence;
14 terminal words; empty content; quote, backslash, Latin-1, and A_2_1.
Index 69 is a newline: the host escapes it, while the core preserves the byte.

Safety boundaries: missing application-area sentinel is BAD_PADDING, as in T059.
Six continuation nibbles already imply base[6]=465010, beyond this dictionary,
so C stops at that point with the decoded prefix. This preserves all defined
valid/index-out-of-range semantics and prevents upstream's possible base-array
overrun/wrapping arithmetic on malformed longer prefixes. The oracle likewise
stops instead of emulating an out-of-bounds base access for its malformed vector.

The checked-in JSC1 resource is unchanged: 1,918,009 bytes, SHA-256
`ced6b30303f004966b29f7e658e7e60c8933526716b1b85c03384d0e9a417149`.
The host CTest verifies this hash, all 1024 offsets, all 262144 records, maximum
length 26, and both Tuple.size quirks before running real-resource waveform tests.
Runtime host validation checks the frozen file size and structural JSC1 header,
then validates touched block bounds and records through the pure callback reader.
SHA identity is a tooling gate, not an engine/runtime hashing dependency.

Portable unit tests use small generated in-memory blocks, not a file or a full
resource in RAM. They cover independent contexts, all header bytes, invalid/truncated
index reads, invalid offsets, oversized/out-of-bounds records, embedded NUL,
callback failures, invalid indices, unchanged outputs, all tail flags and malformed
bits. A 26-byte-word context produces 14 words plus 13 spaces: 377 text bytes.
The 384-byte buffer has a static conservative bound of 14*(26+1)+1 = 379 bytes;
the decode path also checks for OUTPUT_FULL before appending.

On this Linux build, dictionary state is 24 bytes and result storage 388 bytes;
maximum single callback read in the core tests is 32 bytes (the header), never a
cached 4 KB index or whole dictionary. Lookup reads current/next block offsets
for bounds, at most 256 length bytes, and only the selected string bytes.

Host waveform tests apply the existing PHY encoder to fixed application bits,
then compare exact decoded Latin-1 bytes via escaped diagnostics. Missing/corrupt
resources produce `codec=jsc data_error=resource` and exit 1 for JSC frames;
Huffman decoding still succeeds. Default and override paths work from another cwd.

# T060 — JS8 Normal JSC DATA_COMPRESSED RX

Status: READY

## Architect intent

Complete the RX text-codec pair needed for practical JS8 chat.

T059 decodes Normal Huffman DATA (10). T060 decodes Normal DATA_COMPRESSED (11) using the already-generated compact JSC1 receive dictionary.

This task should make the real pinned A_2_1.wav finally print its decoded application text.

Keep TX JSC compression, prefix/list lookup, reassembly, conversations, and UI out of scope.

## Objective

Implement pure bounded RX decompression for JS8 Normal application frames whose first two application bits are:

    11

Decode:

    72 application bits
      -> verify DATA_COMPRESSED prefix
      -> remove upstream padding sentinel
      -> JSC (s,c)-Dense codeword decode
      -> dictionary index j
      -> JSC1 map[j] lookup
      -> bounded Latin-1 text fragment

The final three PHY transmission bits remain T056 metadata and never enter JSC content.

## Accepted baseline

T052-T055: PHY/Linux WAV milestone COMPLETE.
T056: protocol envelope COMPLETE.
T057: heartbeat/CQ/compound identity COMPLETE.
T058: directed header COMPLETE.
T059: Huffman DATA RX + long-WAV first-window host policy COMPLETE.

Read before editing:

    AGENTS.md
    docs/js8/architecture.md
    docs/js8/application-protocol.md
    docs/js8/jsc-dictionary.md
    docs/project/codex/T059-js8-huffman-rx.md

    apps/js8chat/src/js8_engine/js8_protocol_frame.[ch]
    apps/js8chat/src/js8_engine/js8_huffman.[ch]
    apps/js8chat/resources/jsc.dict.meta.json
    apps/js8chat/tools/extract_jsc_dict.py

Frozen reference:

    JS8Call-improved/JS8Call-improved
    tag v3.0.3

Normative source:

    JS8_JSC/JSC.cpp
    JS8_JSC/JSC.h
    JS8_JSC/JSC_map.cpp
    JS8_Main/Varicode.cpp

## Normal compressed DATA framing

Normal legacy DATA uses the same 72-bit framing as T059.

For compressed JSC:

    application bit 0 = 1   data family
    application bit 1 = 1   JSC/compressed
    application bits 2..    JSC bitstream
    padding              = 0 sentinel followed by zero or more 1s

Use the same valid-frame padding rule as T059:

- find the last zero sentinel in application bits 2..71;
- discard sentinel and trailing ones;
- feed only preceding bits 2..sentinel-1 to JSC decompression.

A missing data-area sentinel is an explicit BAD_PADDING error. Do not inherit Qt negative-length corner behavior.

T056 class DATA_COMPRESSED covers both 110 and 111; application bit 2 is content.

## Exact JSC decompression algorithm

Match v3.0.3 JSC::decompress.

Constants:

    b = 4
    s = 7
    c = 2^b - s = 9
    dictionary size = 262144

Decode the unpadded bitstream into JSC codeword bytes:

1. starting at bit 0, read exactly 4 bits into a nibble;
2. append nibble to the temporary codeword-byte stream;
3. if nibble < s:
   - this terminates one dense codeword;
   - if one bit remains, read one separator bit immediately after that nibble;
   - separator=1 means append one literal ASCII space after the dictionary string;
4. if fewer than 4 bits remain where a nibble is required, stop like upstream.

Then decode each dense codeword:

    base[0] = 0
    base[1] = 7
    base[2] = base[1] + 7*9
    base[3] = base[2] + 7*9^2
    ...
    through base[7]

For leading nibbles >= 7:

    j = j*9 + (nibble - 7)

At terminal nibble < 7:

    j = j*7 + terminal + base[k]

where k is the count of leading >=7 nibbles.

If j >= 262144, stop decompression like upstream.

If the bitstream ends before a terminal nibble, stop.

For each valid j, append the full Latin-1 dictionary string map[j].str, then append a space if its terminal nibble had separator=1.

Do not use Tuple.size for RX. The two known declared-size quirks are TX concerns only.

## Streaming decode design

Do not allocate a 70-bit dynamic vector, byte list, separator list, or output list.

Implement the same semantics with bounded local state directly over the 70-bit maximum content region.

A simple one-codeword-at-a-time state machine is preferred.

No heap.

## JSC1 resource interface

The deployed engine must not call fopen, filesystem APIs, MiniShell services, or platform flash APIs.

Add a tiny caller-provided read abstraction, for example:

    typedef int (*Js8JscReadFn)(
        void *context,
        uint32_t offset,
        void *dst,
        size_t bytes);

    typedef struct {
        Js8JscReadFn read;
        void *context;
        uint32_t resource_bytes;
        uint32_t data_offset;
        ...
    } Js8JscDictionary;

Exact API may differ, but preserve:

- engine owns no file/platform access;
- caller owns resource and context;
- dictionary initialization validates the JSC1 header;
- lookup reads only the bytes needed for one index;
- no full 4 KB block index or dictionary is required in RAM;
- no heap;
- multiple dictionary instances are independent.

## JSC1 validation

Validate the frozen JSC1 v1 header:

    magic             "JSC1"
    version           1
    block size        256
    entry count       262144
    block count       1024
    block-index off   32
    flags bit0        Latin-1
    reserved          0

Resource size for the checked-in frozen file:

    1,918,009 bytes

Expected SHA-256, enforced by host/test tooling:

    ced6b30303f004966b29f7e658e7e60c8933526716b1b85c03384d0e9a417149

The pure engine does not need to implement SHA-256. It must validate structural offsets and bounds before lookup.

## JSC1 lookup

For dictionary index j:

    block = j / 256
    slot  = j % 256

Read one little-endian uint32 block offset from:

    index_offset + block*4

Seek logically to that resource offset using the read callback.

Scan exactly slot length-prefixed records:

    uint8 length
    length bytes

Then read the selected record.

Maximum entry length is 26 bytes.

Every read and offset calculation must be bounds checked against resource_bytes.

Return explicit resource/format/bounds errors rather than partial text on resource corruption.

## Bounded output

JSC content has at most 70 bits before padding, and the shortest complete codeword uses:

    4-bit terminal nibble + 1 separator bit = 5 bits

Therefore there are at most 14 decoded dictionary entries.

The frozen dictionary maximum string length is 26 bytes.

A safe pure output bound is therefore:

    14 * 26 = 364 bytes
    + possible separator spaces are already one per codeword
    => conservatively use 384 or 512 bytes including NUL

Choose a fixed compile-time size and prove it cannot overflow. Return OUTPUT_FULL if corrupted/future resource data would exceed it.

Output is Latin-1 bytes. Do not add Unicode transcoding in the engine.

## Suggested status/API

Suggested statuses:

    OK
    INVALID
    WRONG_CLASS
    BAD_PADDING
    BAD_RESOURCE
    BAD_INDEX
    OUTPUT_FULL

Suggested data:

    typedef struct {
        char text[384 or 512];
        uint16_t text_len;
        uint8_t encoded_bit_count;
        uint8_t dictionary_words;
    } Js8JscData;

Suggested API:

    int js8_jsc_dictionary_init(...);
    Js8JscStatus js8_jsc_data_decode(
        const uint8_t payload_bits[JS8_PAYLOAD_BITS],
        const Js8JscDictionary *dict,
        Js8JscData *out);

Invalid/error paths leave output unchanged.

## Host file adapter

Only apps/js8chat/tools/js8_decode.c may use file I/O for the JSC resource.

Add a host FILE-backed read callback.

Default resource should be the checked-in:

    apps/js8chat/resources/jsc.dict

For developer convenience, allow an override such as:

    JS8_JSC_DICT=/path/to/jsc.dict

or a narrow command-line option.

Keep the existing simple invocation working from a normal MiniShell build:

    ./build-linux/js8_decode input.wav

CMake may provide the repository resource path to the host utility as a build-time default. Tests must not depend on current working directory.

If JSC resource initialization fails:

- Huffman and all non-JSC frame decoding must still work;
- for DATA_COMPRESSED, print a clear resource error and do not fabricate text;
- host exit policy should be deterministic and tested.

## Real A_2_1 milestone

The pinned external fixture:

    ~/projects/js8chat/A_2_1.wav

has:

    payload=111001011101001010000111001011100000101011000001100010000111111111111111010
    class=data_compressed
    tx=LAST

T060 must decode its JSC fragment.

Do NOT hard-code the expected text in the task before the first independent decode.

Required process:

1. independently decode that fixed 72-bit application payload with a source-pinned JSC oracle using the exact v3.0.3 JSC algorithm/map;
2. record the resulting expected Latin-1 text in T060 implementation notes;
3. run the MiniShell JSC decoder on the real WAV;
4. require exact equality with the independently derived text;
5. update the optional A_2_1 reference test to lock the text after it is established.

This is the first real DATA_COMPRESSED text milestone.

## Independent JSC oracle

Add an offline oracle that is independent of production MiniShell code.

It should:

- pin SHA-256/Git identity of v3.0.3 JSC.cpp, JSC.h, and JSC_map.cpp;
- parse the upstream map or the source-pinned generated data independently;
- implement the exact dense-codeword decompression formulas;
- generate fixed compressed DATA payload/text vectors;
- decode the T055/T056 A_2_1 payload independently.

The normal CTest must not require the 7 MB upstream source.

Prefer checked-in small vector headers plus the existing checked-in JSC1 resource.

Do not regenerate the 1.9 MB JSC1 file as part of normal CTest.

## Fixed vectors

Include at least:

- one codeword for dictionary index 0;
- dictionary index 6;
- index 7, crossing from base[0] to base[1];
- representative 2-nibble, 3-nibble, and longer dense codewords;
- index 81 to prove RX returns full "@ALLCALL" despite Tuple.size quirk;
- index 262143 to prove RX returns full "ROSIDS";
- multiple codewords with separator bits producing spaces;
- a stream ending on a complete word with no separator;
- a stream with an incomplete trailing nibble;
- a stream with an unterminated >=7 prefix;
- a j >= dictionary size malformed codeword;
- the exact A_2_1 DATA_COMPRESSED payload.

Expected strings must come from the pinned upstream map, not MiniShell.

## Resource corruption tests

Using a small in-memory test reader and/or copies of selected JSC1 bytes, test:

- bad magic
- wrong version
- wrong block size/count
- invalid data/index offsets
- truncated block-index read
- block offset outside resource
- length byte whose record exceeds resource
- index >=262144
- callback read failure
- two dictionary instances with different contexts

Do not modify the checked-in resource during tests.

## Host diagnostics

For DATA_COMPRESSED with a valid resource, append:

    codec=jsc data="..."

Escape double quotes and backslashes in stable diagnostic output so tests remain parseable.

For DATA/Huffman preserve T059:

    codec=huffman data="..."

No association/reassembly yet.

For missing/corrupt JSC resource, use a clear stable marker, e.g.:

    codec=jsc data_error=resource

Do not silently fall back to Huffman for a JSC frame; that would decode the wrong wire format.

## Long WAV behavior

Preserve T059 exactly:

- validate the entire RIFF;
- decode only the first 93 complete 6 kHz blocks;
- count all later decimated samples as ignored;
- no sliding windows.

Do not change this in T060.

## Documentation

Update docs/js8/application-protocol.md and docs/js8/jsc-dictionary.md to record:

- RX JSC decoder implemented;
- pure read-callback ownership boundary;
- DATA_COMPRESSED prefix/padding;
- bounded JSC output;
- A_2_1 independently verified decoded text;
- TX prefix/list packing still pending.

Do not freeze final ADV flash partition layout yet.

## Architectural constraints

- pure C decoder core
- no heap
- no mutable global state
- no Qt/Boost
- no platform/filesystem calls in js8_engine
- no FT8
- fixed output buffers
- caller-supplied resource reader
- Normal only
- JSC1 RX format only

## Non-goals

Do NOT implement:

- JSC TX compression
- prefix[] / list[] TX resource
- Huffman/JSC codec selection for TX
- FIRST/LAST reassembly
- association with directed headers
- compound-directed association
- conversation/application state
- automatic ACK/HB policy
- UI/logging
- MiniShell filesystem/flash binding
- ADV partition layout
- live audio integration
- other JS8 modes

## Acceptance criteria

- [ ] exact v3.0.3 JSC decompression algorithm implemented
- [ ] DATA_COMPRESSED 11 framing and padding decoded
- [ ] caller-provided resource read abstraction; no engine fopen/platform I/O
- [ ] JSC1 header validated structurally
- [ ] direct map[j] lookup from JSC1 implemented
- [ ] index 81 returns full @ALLCALL
- [ ] index 262143 returns full ROSIDS
- [ ] separators reproduce exact spaces
- [ ] incomplete/truncated codeword behavior matches upstream valid semantics
- [ ] corrupt resource/read failures return explicit errors
- [ ] fixed source-pinned JSC vectors checked in
- [ ] independent oracle determines A_2_1 text
- [ ] real A_2_1 WAV prints exact same JSC text
- [ ] optional A_2_1 regression locks that text
- [ ] js8_decode uses checked-in resource by default and supports a narrow override
- [ ] missing JSC resource does not break non-JSC decoding
- [ ] Huffman DATA behavior unchanged
- [ ] long-WAV first-window behavior unchanged
- [ ] T056-T059 semantics unchanged
- [ ] T054/T055 DSP/WAV unchanged
- [ ] js8_engine pure/no-heap
- [ ] no FT8 changes
- [ ] Linux full CTest passes
- [ ] portable CTest passes without host filesystem dependency in core tests
- [ ] external A_2_1 regression passes
- [ ] boundary/no-heap checks pass
- [ ] ASan/UBSan passes
- [ ] ADV build green
- [ ] git diff --check passes
- [ ] no unrelated cleanup

No RF/hardware validation is required.

Because this is the first real JSC text milestone, after supervisor review the architect should manually run on pc-1:

    ./build-linux/js8_decode ~/projects/js8chat/A_2_1.wav

and confirm the printed JSC text before T060 is marked COMPLETE.

## Branch workflow

Use:

    codex/T060-js8-jsc-rx

Codex:

1. read AGENTS.md, canonical JS8 docs, T059, JSC1 docs/resource metadata, and exact v3.0.3 JSC decompressor;
2. implement pure JSC1 resource reader boundary and JSC RX decoder;
3. create independent source-pinned oracle/vectors;
4. independently derive the A_2_1 expected text before locking it in tests;
5. wire host FILE adapter/default resource only in js8_decode;
6. preserve Huffman and long-WAV behavior;
7. do not implement TX JSC or reassembly;
8. run the full existing JS8 gate set plus resource corruption tests;
9. set Status to REVIEW;
10. record exact A_2_1 text and resource evidence;
11. push one reviewable commit and return SHA;
12. no PR and no Actions wait.

## Codex implementation notes

### Implementation summary

### Files changed

### Invariants preserved

### JSC resource / oracle provenance

### First real JSC decode evidence

### Test evidence

### Manual validation still required

### Known limitations / risks

### Commit

## Supervisor review

## Architect test result

Pending manual pc-1 A_2_1 JSC text check after supervisor review.

# T060 — JS8 Normal JSC DATA_COMPRESSED RX

Status: REVIEW

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

- [x] exact v3.0.3 JSC decompression algorithm implemented
- [x] DATA_COMPRESSED 11 framing and padding decoded
- [x] caller-provided resource read abstraction; no engine fopen/platform I/O
- [x] JSC1 header validated structurally
- [x] direct map[j] lookup from JSC1 implemented
- [x] index 81 returns full @ALLCALL
- [x] index 262143 returns full ROSIDS
- [x] separators reproduce exact spaces
- [x] incomplete/truncated codeword behavior matches upstream valid semantics
- [x] corrupt resource/read failures return explicit errors
- [x] fixed source-pinned JSC vectors checked in
- [x] independent oracle determines A_2_1 text
- [x] real A_2_1 WAV prints exact same JSC text
- [x] optional A_2_1 regression locks that text
- [x] js8_decode uses checked-in resource by default and supports a narrow override
- [x] missing JSC resource does not break non-JSC decoding
- [x] Huffman DATA behavior unchanged
- [x] long-WAV first-window behavior unchanged
- [x] T056-T059 semantics unchanged
- [x] T054/T055 DSP/WAV unchanged
- [x] js8_engine pure/no-heap
- [x] no FT8 changes
- [x] Linux full CTest passes
- [x] portable CTest passes without host filesystem dependency in core tests
- [x] external A_2_1 regression passes
- [x] boundary/no-heap checks pass
- [x] ASan/UBSan passes
- [x] ADV build green
- [x] git diff --check passes
- [x] no unrelated cleanup

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

Implemented pure streaming Normal JSC RX, a caller-owned JSC1 read interface,
structural header/lookup validation, fixed Latin-1 output, and explicit errors.
The Linux utility lazily opens the repository dictionary or `JS8_JSC_DICT` override
and appends escaped JSC text. Missing/corrupt resources produce a stable resource
error with exit 1 when needed by a JSC frame; non-JSC decoding remains available.

The independent oracle first derived the real A_2_1 text as **`MSG ID 416`** before
the C decoder was implemented. The real WAV now produces that exact text.
No scope deviations; bounded malformed-input differences are documented below.

### Files changed

- `apps/js8chat/src/js8_engine/js8_jsc.[ch]`: pure JSC1 initialization/lookup,
  streaming dense decompression, bounded output and statuses.
- `apps/js8chat/tools/js8_decode.c`: FILE adapter, lazy default/override resource,
  escaped Latin-1 JSC diagnostics and deterministic resource-error exit policy.
- `tests/js8_jsc_oracle.py`, `tests/js8_jsc_vectors.h`, `tests/js8_jsc_vectors.md`:
  independent source-pinned oracle, small fixed vectors and provenance.
- `tests/js8_jsc_test.c`: filesystem-free virtual-reader tests, corrupt resources,
  independent contexts, malformed codewords, capacity bound, and waveform fixtures.
- `tests/js8_jsc_wav_test.py`: frozen-resource hash/full structure checks, 21
  end-to-end vectors, cwd independence, overrides and missing/corrupt-resource tests.
- `tests/js8_wav_reference.py`: lock real A_2_1 JSC text with the default resource.
- `tests/js8_tests.cmake`, `CMakeLists.txt`: library/unit/host regression wiring and
  build-time default dictionary path.
- `docs/js8/application-protocol.md`, `docs/js8/jsc-dictionary.md`: implemented RX
  ownership, framing, resource/error contract, verified text and pending TX work.
- This task: REVIEW handoff and evidence.

### Invariants preserved

No heap, mutable global state, filesystem/platform calls, dynamic vectors, or
index/dictionary cache in the engine. Caller owns the immutable resource/context.
Init validates every frozen header field; lookup validates current/next block
bounds, each scanned record length, selected bytes and callback success. Reads are
bounded against resource_bytes before calling the reader. Every error preserves
caller outputs, including failures after decoding has begun (result is local).

All 75 bits are validated through T056; only 11 content enters JSC. Separators,
terminal-without-separator, partial nibble, unterminated word and out-of-range index
match upstream defined RX semantics. Full map strings are used, not Tuple.size.
The output bound is at most 14 entries plus spaces: conservative 14*(26+1)+1=379
bytes including NUL fits 384, backed by static assertion and append checks. A test
with 14 maximum-length strings and 13 spaces returns exactly 377 text bytes.

Malformed-input safety: absent sentinel returns BAD_PADDING. Six continuation
nibbles already imply base[6]=465010, beyond this map; stop before overflow or the
upstream base-array overrun possible for even longer malformed sequences. Valid
codewords and defined out-of-range stops are unchanged. No other behavior deviation.

Huffman, directed/compound/envelope semantics, T054 DSP, T059 whole-RIFF validation,
phase-0 decimation and first-93-block long-WAV policy remain unchanged. No FT8,
TX compression/prefix/list, association/reassembly, conversations, UI or ADV
partition-layout change. The existing JSC1 file/metadata remain byte-for-byte intact.

### JSC resource / oracle provenance

Frozen source repository/tag: JS8Call-improved/JS8Call-improved v3.0.3.
SHA-256 pins verified by the independent oracle:

- JSC.cpp: `0f1c974a96fd65e043b1a4dbdb69a9ae43e42dafe81f5f22f196b09596dbcdeb`
- JSC.h: `3edfda65865dc4ede66730113c1c7250861253d78ed83f17626ce55c79b0f1d8`
- JSC_map.cpp: `ab2bd62ef594f4629a2c93b6de43f5469b1fd6fe67ebf4d24f915bd11ccef813`

The oracle independently parses upstream map strings and reproduces its two-pass
nibble/separator decode. Production instead streams bounded state. No production
MiniShell code or extractor is called by the oracle. Fixed vectors cover all task
cases plus newline, quote, backslash and non-ASCII Latin-1. Normal tests need no
upstream checkout and never regenerate the resource.

JSC1 evidence: 1,918,009 bytes; SHA-256
`ced6b30303f004966b29f7e658e7e60c8933526716b1b85c03384d0e9a417149`.
Host test tooling enforces this hash and verifies all 1024 block offsets and 262144
records, max length 26, including index 81 `@ALLCALL` and index 262143 `ROSIDS`.
Runtime validation is structural, not a SHA implementation; overrides are not hashed.
Measured Linux dictionary state is 24 bytes, output struct 388 bytes, maximum single
core callback read 32 bytes. Lookup uses at most two adjacent index entries and
256 length-byte reads, then the selected string; no 4 KB index cache is needed.

### First real JSC decode evidence

Independent oracle ran first:

```sh
python3 tests/js8_jsc_oracle.py /tmp/T060-JSC.cpp /tmp/T060-JSC.h /tmp/T060-JSC_map.cpp > tests/js8_jsc_vectors.h
```

```text
A_2_1 independent Latin-1 text: b'MSG ID 416'
A_2_1 text hex: 4d534720494420343136
```

The unpadded content has 54 bits and six dictionary words. The pure vectors, host
synthetic fixture and real WAV all match the independent expected Latin-1 bytes.
Real-WAV stdout:

```text
payload=111001011101001010000111001011100000101011000001100010000111111111111111010 type=2 frame="vTA7BWh1Y7++" tx_raw=2 class=data_compressed tx=LAST score=26 time=5/0 freq=57/0 hz=556.250 hard_errors=15 codec=jsc data="MSG ID 416"
```

Real-WAV stderr:

```text
candidate=0 score=26 time=5/0 freq=57/0 status=0
candidate=1 score=14 time=5/0 freq=57/1 status=-2
candidate=2 score=12 time=18/0 freq=54/0 status=-2
candidate=3 score=10 time=5/0 freq=56/1 status=-2
candidate=4 score=10 time=17/0 freq=110/1 status=-2
blocks=93 ignored_engine_samples=720 candidates=50 ldpc_fail=49 crc_fail=0 valid=1 unique=1
```

The WAV blob remains `d986a4e5a9cc654dffbfadae73ec35cc9cea1d83`, outside the
repository and uncommitted. PHY payload/candidate diagnostics are unchanged.

### Test evidence

Commands/results on 2026-09-22:

```sh
cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure
# Initial run and ordinary full rerun: 102/103; existing linux_serial_unit
# intermittently failed its PTY write timeout assertion (line 67).
ctest --test-dir build-linux -R '^linux_serial_unit$' --repeat until-pass:3 --output-on-failure
# Passed first attempt in this targeted invocation.
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --repeat until-pass:3 --output-on-failure
# Final full run: 103/103, every test passed its first attempt; no repeats needed.

cmake -S tests/unit -B /tmp/T060-build-unit
cmake --build /tmp/T060-build-unit -j"$(nproc)"
ctest --test-dir /tmp/T060-build-unit --output-on-failure
# 23/23. Core JSC tests use only a virtual in-memory reader.

PYTHONDONTWRITEBYTECODE=1 python3 tests/app_dependency_boundary.py . js8chat
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_platform_boundary.py . js8chat
# Both PASS; full CTest includes compiled-library no-heap checks.

cmake -S . -B /tmp/T060-build-ref -DJS8_A2_1_REFERENCE_WAV="$HOME/projects/js8chat/A_2_1.wav"
cmake --build /tmp/T060-build-ref -j"$(nproc)"
ctest --test-dir /tmp/T060-build-ref -R 'js8.*reference|js8.*A2.*1' --output-on-failure
# 1/1, exact MSG ID 416 locked.
/tmp/T060-build-ref/js8_decode "$HOME/projects/js8chat/A_2_1.wav"
git hash-object ~/projects/js8chat/A_2_1.wav
sha256sum apps/js8chat/resources/jsc.dict
# Hashes match above.

cmake -S . -B /tmp/T060-build-sanitize \
  -DCMAKE_C_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer -g' \
  -DJS8_A2_1_REFERENCE_WAV="$HOME/projects/js8chat/A_2_1.wav"
cmake --build /tmp/T060-build-sanitize -j"$(nproc)" \
  --target js8_rx_unit js8_phy_unit js8_frame_unit js8_protocol_frame_unit js8_compound_unit js8_directed_unit js8_huffman_unit js8_jsc_unit js8_decode
ctest --test-dir /tmp/T060-build-sanitize \
  -R '^js8_(phy_unit|rx_unit|frame_unit|protocol_frame_unit|compound_unit|directed_unit|huffman_unit|jsc_unit|wav_unit|directed_wav_unit|huffman_wav_unit|jsc_wav_unit|A2_1_reference)$' \
  --output-on-failure
# 13/13 including corruption tests; outside sandbox for LeakSanitizer compatibility.

JS8_JSC_DICT=/tmp/T060-missing-dictionary ctest --test-dir build-linux \
  -R '^js8_(wav_unit|directed_wav_unit|huffman_wav_unit)$' --output-on-failure
# 3/3, covering non-JSC classes with no resource.

source ~/projects/esp-idf/export.sh
idf.py -C platform/adv build
# PASS; image 0x151790 bytes, app partition 78% free.

python3 tests/js8_jsc_oracle.py /tmp/T060-JSC.cpp /tmp/T060-JSC.h /tmp/T060-JSC_map.cpp > /tmp/T060-regenerated.h
cmp tests/js8_jsc_vectors.h /tmp/T060-regenerated.h
# Identical.
git diff --check
# PASS.
```

No serial code/test or gate was changed to address the intermittent unrelated PTY
failure. Corruption tests cover every header byte, truncated index callback,
out-of-range/reversed block offsets, record length beyond resource/block bounds,
length >26, embedded NUL, invalid index, callback failures, independent contexts,
and unchanged outputs. Host tests exercise missing, empty-path, corrupt and
truncated resource overrides without modifying the checked-in dictionary.

### Manual validation still required

After supervisor review, the architect must run on pc-1:

```sh
./build-linux/js8_decode ~/projects/js8chat/A_2_1.wav
```

Confirm `codec=jsc data="MSG ID 416"` before marking T060 COMPLETE. No RF/hardware
acceptance is required; this manual host check remains pending.

### Known limitations / risks

The core checks structure/bounds, not resource authenticity; frozen SHA identity is
enforced by test tooling. Caller must keep the resource/context immutable and valid.
Runtime host overrides receive structural validation only. Unrelated serial PTY
intermittency is recorded above. JSC TX and prefix/list resources remain pending;
text fragments have no directed association or reassembly. ADV resource binding and
partition choices are still deferred.

### Commit

One reviewable commit on `codex/T060-js8-jsc-rx` containing these notes; SHA returned
after push. No PR or GitHub Actions wait.

## Supervisor review

## Architect test result

Pending manual pc-1 A_2_1 JSC text check after supervisor review.

# T059 — JS8 Normal Huffman DATA RX decoder

Status: REVIEW

## Architect intent

Add the first continuation-text codec needed for practical JS8 keyboard chat.

T058 can decode a standard DIRECTED header such as:

    AG6AQ -> K1ABC cmd=" "

but the following message text is carried in DATA frames. T059 implements only the Normal legacy Huffman DATA family.

JSC/compressed DATA remains the next task.

No reassembly or conversation state yet.

## Objective

Implement pure RX decoding for JS8 Normal application frames whose first two application bits are:

    10

These are v3.0.3 Huffman DATA frames.

Decode:

    72 application bits
      -> verify DATA / non-compressed prefix
      -> remove upstream end padding
      -> Huffman decode
      -> bounded text fragment

Do not decode the final three PHY transmission flags as content; FIRST/LAST remain T056 envelope metadata.

## Accepted baseline

T052-T055: PHY/Linux WAV milestone COMPLETE.
T056: protocol envelope COMPLETE.
T057: heartbeat/CQ/compound identity COMPLETE.
T058: standard directed header COMPLETE.

Read before editing:

    AGENTS.md
    docs/js8/architecture.md
    docs/js8/application-protocol.md
    docs/project/codex/T058-js8-directed.md

    apps/js8chat/src/js8_engine/js8_protocol_frame.[ch]
    apps/js8chat/src/js8_engine/js8_directed.[ch]

Frozen reference:

    JS8Call-improved/JS8Call-improved
    tag v3.0.3

Normative source:

    JS8_Main/Varicode.cpp
    JS8_Main/Varicode.h

Normative functions/data:

    hufftable
    Varicode::huffDecode
    packHuffMessage
    packDataMessage
    unpackDataMessage

## Normal DATA framing

The 72 application bits are packed as:

    bit 0 = 1     data-family marker
    bit 1 = 0     Huffman, not JSC/compressed
    bits 2..      Huffman payload
    final padding = one 0 sentinel followed by zero or more 1 bits

Upstream packHuffMessage always uses prefix:

    10

and only appends another Huffman code when:

    current_bits + next_code_bits < 72

Therefore a valid transmitted frame always has at least one padding bit.

The upstream decoder:

1. requires application bit 0 = 1;
2. reads application bit 1 as compressed selector;
3. finds the final zero used as the padding sentinel;
4. removes the two prefix bits and the sentinel/trailing-one padding;
5. for selector 0, Huffman-decodes the remaining bit vector.

T059 supports selector 0 only.

T056 class normalization already maps 100/101 to DATA. The third application bit is the first Huffman content bit, not another class bit.

## Exact Huffman table

Use the exact v3.0.3 table:

    " "  01
    E    100
    T    1101
    A    0011
    O    11111
    I    11100
    N    10111
    S    10100
    H    00011
    R    00000
    D    111011
    L    110011
    C    110001
    U    101101
    M    101011
    W    001011
    F    001001
    G    000101
    Y    000011
    P    1111011
    B    1111001
    .    1110100
    V    1100101
    K    1100100
    -    1100001
    +    1100000
    ?    1011001
    !    1011000
    "    1010101
    X    1010100
    0    0010101
    J    0010100
    1    0010001
    Q    0010000
    2    0001001
    Z    0001000
    3    0000101
    5    0000100
    4    11110101
    9    11110100
    8    11110001
    6    11110000
    7    11101011
    /    11101010

No lowercase output. No escape extension is part of this table.

Do not redesign with a dynamic tree. A small static table or bounded bitwise decoder is preferred.

## Decode semantics

A suitable output:

    #define JS8_HUFF_TEXT_MAX 35

    typedef struct {
        char text[JS8_HUFF_TEXT_MAX];
        uint8_t text_len;
        uint8_t encoded_bit_count;
    } Js8HuffmanData;

Exact sizes may differ, but output must be fixed/bounded and no heap.

A suitable API:

    int js8_huffman_data_decode(
        const uint8_t payload_bits[JS8_PAYLOAD_BITS],
        Js8HuffmanData *out);

Requirements:

- validate all 75 bits;
- use T056 envelope classification;
- require application class DATA, not DATA_COMPRESSED;
- require first application bits exactly 10;
- ignore final PHY tx flags for content decode;
- remove padding according to upstream Normal framing;
- Huffman-decode the remaining content;
- NUL-terminate text;
- leave output unchanged on invalid arguments/class.

Maximum decoded fragment length is bounded by the 70 data-area bits and shortest 2-bit code; choose a conservative compile-time output capacity and prove it by test/static assertion.

## Huffman partial-code behavior

Match v3.0.3 huffDecode behavior for a valid unpadded content vector:

- consume matching codewords from the start;
- if remaining bits do not start with any complete table code, stop;
- return the text decoded so far.

Do not fabricate a replacement character and do not continue past an undecodable suffix.

Padding extraction itself must remain safe and bounded.

For malformed DATA padding that cannot correspond to an upstream packHuffMessage frame, return an explicit decode error rather than reading outside bounds. Document any deliberate malformed-input safety divergence from Qt container corner behavior; valid v3.0.3 frames must match exactly.

## Status

Prefer explicit private statuses such as:

    OK
    INVALID
    WRONG_CLASS
    COMPRESSED
    BAD_PADDING
    OUTPUT_FULL

Exact enum names may differ.

COMPRESSED should be distinct so T060 can own it later.

Do not silently return empty text for a JSC frame.

## Golden vectors

Create source-pinned independent vectors from exact v3.0.3 behavior for at least:

1. "HELLO"
2. "HELLO WORLD"
3. "CQ FIELD"
4. "ACK"
5. "73"
6. "AG6AQ"
7. "K1ABC/P"
8. "THE QUICK"
9. a fragment dominated by spaces to exercise shortest code and maximum text count
10. a fragment that fills the frame close to the 72-bit limit
11. one valid frame with an intentionally truncated final Huffman code before padding, if reproducible as a decoder-only vector, to verify partial-decode stop behavior

For each fixed vector record:

    input text used by upstream encoder
    exact 72 application bits
    number of source characters consumed by packHuffMessage
    expected decoded fragment
    full 75-bit payload with independently varied transmission flags where useful

Generate vectors with an offline source-pinned oracle that transcribes/executes the v3.0.3 Huffman algorithm independently of MiniShell production code.

Pin Varicode.cpp/.h hashes. Normal CTest must not require Qt/upstream.

## Exhaustive table tests

Prove every one of the 44 Huffman entries decodes from its exact bit code.

Also prove:

- table is prefix-free for all valid entries;
- output uses exact uppercase/punctuation characters;
- trailing incomplete bit prefixes stop without extra output;
- padding sentinel/trailing ones are not returned as text;
- all eight tail transmission flag values do not alter decoded text.

## Longer host WAV input for WebSDR captures

T059 also changes only the Linux host tool's input-window policy so the architect can test longer WebSDR recordings.

Current T055 behavior rejects files containing more than 93 complete 6 kHz engine blocks. Replace that host-only rejection with deterministic truncation:

    WAV may contain >= 1 complete engine block
    process_blocks = min(total_complete_blocks, 93)
    process exactly the first process_blocks
    ignore every remaining engine sample after block 93

For a longer WAV, decoding therefore uses only the **first 93 complete 960-sample engine blocks** from the beginning of the file.

This is deliberately not a sliding-window or multi-slot decoder yet.

Requirements:

- preserve the existing 12 kHz mono S16 WAV contract;
- preserve continuous phase-0 2:1 decimation from WAV sample zero;
- never allocate the whole WAV;
- stop feeding the monitor after block 93;
- safely ignore/skip the remaining WAV data;
- do not pad an incomplete block;
- report the amount of input ignored clearly in host diagnostics;
- the original pinned 15-second A_2_1 behavior/output must remain unchanged;
- no change to Js8Monitor capacity or deployed js8_engine behavior.

Suggested summary diagnostics may distinguish:

    processed_blocks=93
    ignored_engine_samples=<all samples after block 93>

or retain the existing `blocks=` field and redefine/document `ignored_engine_samples` to include both the old partial-tail remainder and all complete samples beyond the 93-block host window.

The exact field names may remain backward-compatible, but tests must make the semantics unambiguous.

Add host tests for at least:

1. exact 93-block input;
2. 93 blocks + the existing 720-sample tail;
3. 94 complete blocks;
4. a substantially longer recording, e.g. 2-3 Normal periods;
5. signal inside the first 93-block window still decodes normally;
6. data after block 93 cannot affect the decode result;
7. truncated/malformed trailing RIFF data is still rejected by the existing container validation rather than hidden by early DSP truncation.

This feature is specifically for convenient WebSDR experimentation. Searching later windows of a long capture is a separate future task.

## Host diagnostics

Extend js8_decode for DATA frames only.

Append stable output such as:

    data="HELLO WORLD"

For DATA_COMPRESSED, leave T056 output unchanged for now. Optionally append:

    codec=jsc

but do not attempt JSC decode in T059.

For Huffman DATA append:

    codec=huffman data="..."

Do not associate this fragment with a prior directed header. That is later reassembly/application state.

The real A_2_1 WAV is DATA_COMPRESSED, so its stable external reference output must remain unchanged except for an optional non-semantic codec label if tests are intentionally updated. Prefer no change.

## Synthetic host integration

Add several end-to-end synthetic WAV regressions built from the independently fixed Huffman application payloads:

    fixed 75-bit payload
      -> existing JS8 PHY encoder for test waveform only
      -> js8_decode
      -> class=data
      -> codec=huffman
      -> exact expected data fragment

The existing PHY encoder is only an integration fixture generator, not the application golden oracle.

## Documentation

Update docs/js8/application-protocol.md with:

- Normal legacy DATA prefix 10;
- exact end-padding rule;
- Huffman decoder ownership;
- explicit note that a decoded DATA fragment is not yet a conversation message until associated/reassembled with directed context and FIRST/LAST.

Do not alter frozen application scope.

## Architectural constraints

- pure C
- no heap
- no mutable global state
- no Qt/Boost/JSC
- no MiniShell/platform
- no FT8
- no reassembly/conversation state
- no application TX packer in production
- Normal only
- fixed bounded buffers

## Non-goals

Do NOT implement:

- JSC / DATA_COMPRESSED decode
- Huffman TX API for production
- FIRST/LAST reassembly
- association with a directed header
- compound-directed association
- conversation history
- scheduler/auto replies
- UI/logging
- MiniShell live integration
- other JS8 modes

## Acceptance criteria

- [x] exact 44-entry v3.0.3 Huffman table implemented
- [x] Normal 10 DATA framing decoded
- [x] upstream padding removed exactly for valid frames
- [x] partial undecodable suffix stops like upstream huffDecode
- [x] JSC/compressed frames return distinct unsupported/compressed status
- [x] fixed upstream-derived vectors checked in
- [x] all 44 table entries individually tested
- [x] prefix-free property tested
- [x] tail transmission flags remain independent
- [x] js8_decode prints exact Huffman fragment for DATA frames
- [x] synthetic WAV integrations pass
- [x] js8_decode accepts WAVs longer than 93 engine blocks
- [x] long WAVs are deterministically truncated to the first 93 complete blocks
- [x] samples after the first 93 blocks cannot affect decode output
- [x] long-WAV ignored-sample diagnostics are tested
- [x] real A_2_1 output remains unchanged
- [x] T056-T058 semantics unchanged
- [x] T054/T055 DSP/WAV unchanged
- [x] js8_engine pure/no-heap
- [x] no FT8 changes
- [x] Linux full CTest passes
- [x] portable CTest passes
- [x] external A_2_1 regression passes
- [x] boundary/no-heap checks pass
- [x] ASan/UBSan passes
- [x] ADV build green
- [x] git diff --check passes
- [x] no unrelated cleanup

No hardware/RF validation required.

## Branch workflow

Use:

    codex/T059-js8-huffman-rx

Codex:

1. read AGENTS.md, canonical JS8 docs, T058, and exact v3.0.3 Huffman/data functions;
2. implement a pure bounded Huffman DATA RX decoder only;
3. build independent source-pinned vectors;
4. test all table entries and framing/padding behavior;
5. extend js8_decode diagnostics for DATA only;
6. change host WAV handling so recordings longer than one window are truncated to the first 93 complete engine blocks, with tested ignored-input diagnostics;
7. do not implement sliding windows, multi-slot decode, JSC, or reassembly;
8. run the full existing JS8 gate set;
9. set Status to REVIEW;
10. fill notes/vector provenance;
11. push one reviewable commit and return SHA;
12. no PR and no Actions wait.

## Codex implementation notes

### Implementation summary

Added a bounded pure Normal Huffman DATA RX decoder with the exact 44-entry table,
explicit invalid/wrong-class/compressed/bad-padding statuses, and upstream partial-code
stop behavior. Host DATA diagnostics print `codec=huffman data="..."`, escaping
quotation marks; malformed padding prints `data_error=bad_padding` while retaining
the valid PHY payload diagnostics. Compressed DATA output remains unchanged.

Implemented the explicitly authorized host-only long-WAV policy: process the first
min(total complete blocks, 93), count every remaining decimated sample as ignored,
and leave whole-container RIFF validation in front of DSP. No sliding windows.
No deviations beyond the task-required malformed-padding safety behavior below.

### Files changed

- `apps/js8chat/src/js8_engine/js8_huffman.[ch]`: pure RX decoder, immutable table,
  explicit statuses and fixed output buffer.
- `apps/js8chat/tools/js8_decode.c`: Huffman diagnostics and first-window truncation.
- `tests/js8_huffman_oracle.py`, `tests/js8_huffman_vectors.h`,
  `tests/js8_huffman_vectors.md`: independent pinned vectors and provenance.
- `tests/js8_huffman_test.c`: exact table, prefix freedom, incomplete suffixes,
  padding positions, capacity bound, tail flags and invalid-input canaries;
  test-only PHY waveform fixture mode.
- `tests/js8_huffman_wav_test.py`: thirteen synthetic end-to-end payload/text tests.
- `tests/js8_wav_test.py`: exact 93-block, partial-tail, 94-block, and nearly
  three-period inputs; later signals/noise cannot affect the first window;
  malformed/truncated trailing data still fails.
- `tests/js8_tests.cmake`, `CMakeLists.txt`: module/unit/host gate integration.
- `docs/js8/application-protocol.md`: Huffman framing/ownership, fragment boundary,
  and host long-recording/ignored-sample policy.
- This task: REVIEW handoff and evidence.

### Invariants preserved

Pure C, no heap or mutable global state, no platform/FT8 dependencies. Output stays
unchanged on every error. The two-bit `10` prefix is independent of the third bit;
all 75 bits are validated and tail flags never become text. At least one sentinel
leaves at most 69 content bits; the shortest code is two bits, so at most 34 text
characters plus NUL fit the 35-byte buffer. Static assertion and maximal-space
fixture establish this bound. Incomplete suffixes stop without replacement output.

Deliberate safety behavior: `10` plus 70 ones returns BAD_PADDING because the data
area has no sentinel. Upstream can use the selector zero and Qt's negative-length
`mid` behavior to expose those 70 bits. Such input cannot come from packHuffMessage;
valid frames and decoder-only incomplete-code cases retain exact upstream behavior.
An empty fragment with a real sentinel at bit 2 remains valid.

T056-T058 production modules and existing semantic tests are unchanged. T054 DSP,
monitor capacity and the 12 kHz mono S16/phase-0 contract remain unchanged. The only
T055 WAV-policy change is the long-input truncation expressly authorized in T059;
its previous >93-block rejection test is replaced with acceptance/ignored-tail tests.
No whole-recording allocation, padding of partial blocks, or skipping RIFF validation.
No JSC, reassembly, directed association, conversations, auto-replies or production TX.

### Golden-vector provenance

Read exact v3.0.3 `Varicode.cpp/.h`: hufftable, huffEncode/huffDecode,
packHuffMessage, packDataMessage, and unpackDataMessage. SHA-256 pins and regeneration
commands are documented in `tests/js8_huffman_vectors.md`.

The source-pinned Python oracle extracts all 44 entries and independently transcribes
upstream packing, sentinel removal, and sorted QMap decoding. It forces the Huffman
branch rather than implementing packDataMessage's Huffman/JSC competition. The header
records each input, 72 application bits, consumed count, expected fragment, unpadded
bit count and full payload. All requested strings, 34 spaces, a 69-bit near-full
frame, quotes, empty content and a decoder-only HE + incomplete suffix are checked in.
The latter has NULL source/-1 consumed to distinguish it from encoder output.

The host fixture uses existing PHY encoding only to synthesize audio from fixed
application bits; it is not the independent application oracle. Normal tests require
neither Qt nor an upstream checkout. Offline regeneration matches byte-for-byte.

### Test evidence

Final commands/results on 2026-09-22:

```sh
cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure
# Final full rerun: 101/101, no external WAV configured.

cmake -S tests/unit -B /tmp/T059-build-unit
cmake --build /tmp/T059-build-unit -j"$(nproc)"
ctest --test-dir /tmp/T059-build-unit --output-on-failure
# 22/22.

PYTHONDONTWRITEBYTECODE=1 python3 tests/app_dependency_boundary.py . js8chat
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_platform_boundary.py . js8chat
# Both PASS; full CTest also includes compiled-library no-heap checks.

cmake -S . -B /tmp/T059-build-ref -DJS8_A2_1_REFERENCE_WAV="$HOME/projects/js8chat/A_2_1.wav"
cmake --build /tmp/T059-build-ref -j"$(nproc)"
ctest --test-dir /tmp/T059-build-ref -R 'js8.*reference|js8.*A2.*1' --output-on-failure
# 1/1.
git hash-object ~/projects/js8chat/A_2_1.wav
# d986a4e5a9cc654dffbfadae73ec35cc9cea1d83
/tmp/T059-build-ref/js8_decode "$HOME/projects/js8chat/A_2_1.wav"

cmake -S . -B /tmp/T059-build-sanitize \
  -DCMAKE_C_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer -g' \
  -DJS8_A2_1_REFERENCE_WAV="$HOME/projects/js8chat/A_2_1.wav"
cmake --build /tmp/T059-build-sanitize -j"$(nproc)" \
  --target js8_rx_unit js8_phy_unit js8_frame_unit js8_protocol_frame_unit js8_compound_unit js8_directed_unit js8_huffman_unit js8_decode
ctest --test-dir /tmp/T059-build-sanitize \
  -R '^js8_(phy_unit|rx_unit|frame_unit|protocol_frame_unit|compound_unit|directed_unit|huffman_unit|wav_unit|directed_wav_unit|huffman_wav_unit|A2_1_reference)$' \
  --output-on-failure
# 11/11, outside sandbox for LeakSanitizer ptrace compatibility.

source ~/projects/esp-idf/export.sh
idf.py -C platform/adv build
# PASS; image 0x151790 bytes, app partition 78% free.

python3 tests/js8_huffman_oracle.py /tmp/T056-Varicode.cpp /tmp/T056-Varicode.h > /tmp/T059-regenerated.h
cmp tests/js8_huffman_vectors.h /tmp/T059-regenerated.h
# Identical.
git diff --check
# PASS.
```

The initial full Linux run passed 100/101 but hit the existing serial PTY write
queue assertion at `tests/linux_serial_test.c:67`; one isolated retry also failed.
`ctest --test-dir build-linux -R '^linux_serial_unit$' --repeat until-pass:3
--output-on-failure` then passed on its first attempt, and the subsequent ordinary
full CTest passed 101/101. No serial code/test or gate was modified or weakened.

Long-WAV regressions assert `blocks=93` with ignored counts 0 (exact window), 720
(old partial tail), 960 (94 blocks), and 180000 (two more 15-second periods).
The latter files preserve identical decoded stdout despite later signals/full-scale
samples. A signal only after the first window yields no decoded output. Truncated
long data and malformed chunks after long data still return failure.

Exact real-WAV stdout, byte-for-byte identical to T058:

```text
payload=111001011101001010000111001011100000101011000001100010000111111111111111010 type=2 frame="vTA7BWh1Y7++" tx_raw=2 class=data_compressed tx=LAST score=26 time=5/0 freq=57/0 hz=556.250 hard_errors=15
```

Summary remains `blocks=93 ignored_engine_samples=720 candidates=50 ldpc_fail=49
crc_fail=0 valid=1 unique=1`. The WAV stays external and uncommitted.

### Manual validation still required

None for T059 acceptance. Optional architect WebSDR experiments can use longer files,
with the documented first-window limitation; no hardware/RF gate is required.

### Known limitations / risks

Only Huffman DATA is decoded. Fragments have no directed context or reassembly;
JSC has a distinct unsupported status. The caller owns PHY CRC validity.
Later windows in long recordings are intentionally ignored. The unrelated serial
PTY gate showed intermittent failure before passing the final full rerun, as above.

### Commit

One reviewable commit on `codex/T059-js8-huffman-rx` containing these notes; SHA
returned after push. No PR or GitHub Actions wait.

## Supervisor review

## Architect test result

No hardware/RF acceptance required.

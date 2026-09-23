# T058 — JS8 standard directed frame decoder

Status: REVIEW

## Architect intent

Continue from T057 into the standard directed-message header used for practical one-to-one JS8 chat.

This task decodes sender, destination, portable flags, directed command, and optional numeric field. It must explicitly support the v0.1-required free-text command, ACK, and 73. For interoperability and little extra cost, decode all 0..31 upstream command codes to stable names, but do not implement automatic behavior for query/relay/store-forward commands that are outside v0.1.

No continuation data, Huffman/JSC, reassembly, conversations, or UI yet. JS8Chat remains Normal/Mode A only.

## Objective

Implement pure RX decoding for v3.0.3 FrameDirected application payloads.

The 72 application bits are:

    bits  0..2   class = 011 DIRECTED
    bits  3..30  from_callsign28
    bits 31..58  to_callsign28
    bits 59..63  command5
    bit  64      portable_from
    bit  65      portable_to
    bits 66..71  number6

Equivalent upstream layout:

    [3][28][28][5],[2][6]

The final PHY transmission bits 72..74 remain independent T056 flags.

## Accepted baseline

T052-T055: PHY/Linux WAV milestone COMPLETE.
T056: protocol envelope COMPLETE.
T057: heartbeat/CQ/compound identity COMPLETE.

Read AGENTS.md, docs/js8/architecture.md, docs/js8/application-protocol.md, T056/T057, and current JS8 engine protocol modules.

Frozen reference:

    JS8Call-improved/JS8Call-improved
    tag v3.0.3

Normative functions/data:

    Varicode::unpackCallsign
    Varicode::unpackDirectedMessage
    Varicode::unpackCmd
    Varicode::formatSNR
    directed_cmds
    basecalls
    nbasecall = 262177560

## 28-bit callsign decoding

Implement a pure v3.0.3-compatible unpacker for every 28-bit value.

Generic mixed-radix decode:

    final chars 3..5: radix 27 with +10 alphabet offset
    char 2:            radix 10
    char 1:            radix 36
    char 0:            remaining value

Use upstream alphabet:

    0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ /@

Before generic unpacking, match the complete v3.0.3 special basecalls table at nbasecall+1 through nbasecall+54, including <....>, @ALLCALL, @HB, @POTA, @QRP, @QRO, and all intermediate entries.

Supporting decode of reserved/group names is wire-format compatibility only; T058 does not enable group-operation policy.

Important upstream behavior:

- special basecalls return immediately and ignore the portable flag;
- generic 3D0... expands to 3DA0...;
- generic Q followed by A-Z expands to 3X...;
- generic output is trimmed;
- if portable is true, append /P after trimming.

Choose fixed output storage large enough for the longest special string plus NUL.

No heap.

## Directed command decoding

The 5-bit command code is 0..31. Provide stable canonical v3.0.3 names matching QMap::key behavior:

    0  " SNR?"
    1  " DIT DIT"
    2  " NACK"
    3  " HEARING?"
    4  " GRID?"
    5  ">"
    6  " STATUS?"
    7  " STATUS"
    8  " HEARING"
    9  " MSG"
    10 " MSG TO:"
    11 " QUERY"
    12 " QUERY MSGS"
    13 " QUERY CALL"
    14 " ACK"
    15 " GRID"
    16 " INFO?"
    17 " INFO"
    18 " FB"
    19 " HW CPY?"
    20 " SK"
    21 " RR"
    22 " QSL?"
    23 " QSL"
    24 " CMD"
    25 " SNR"
    26 " NO"
    27 " YES"
    28 " 73"
    29 " HEARTBEAT SNR"
    30 " AGN?"
    31 " "

Leading spaces are part of the canonical strings.

At minimum expose command_code, command string, is_free_text for code 31, is_ack for code 14, is_73 for code 28, and is_snr for codes 25/29.

Do not implement command actions or policy.

## Optional numeric field

Low six bits of extra8 are the packed number.

Receive semantics:

    extra6 == 0 -> no number
    extra6 != 0 -> number = extra6 - 31

Expose has_number and signed number. Preserve arbitrary wire values, including the possible decoded +32.

For SNR commands a presentation helper may match upstream formatting, e.g. -8 -> -08, +5 -> +05, 0 -> +00. Core semantic value remains integer.

## Directed output

Suggested pure type:

    typedef struct {
        char from[...];
        char to[...];
        uint32_t from_packed;
        uint32_t to_packed;
        uint8_t command_code;
        uint8_t portable_from;
        uint8_t portable_to;
        int has_number;
        int number;
    } Js8DirectedFrame;

Suggested API:

    int js8_directed_decode(
        const uint8_t payload_bits[JS8_PAYLOAD_BITS],
        Js8DirectedFrame *out);

Requirements:

- validate all 75 bits;
- require DIRECTED class;
- use T056 envelope classification;
- transmission FIRST/LAST flags do not affect content decoding;
- invalid input/class leaves output unchanged;
- no CRC work here; caller owns validated PHY payloads.

## Special placeholder

The special value <....> = nbasecall+1 is important for compound-call workflows and must decode exactly.

Do not implement compound association/state in T058.

## Golden vectors

Add source-pinned independently generated vectors for at least:

1. AG6AQ -> K1ABC free-text, no number
2. AG6AQ -> K1ABC ACK
3. AG6AQ -> K1ABC 73
4. AG6AQ/P -> K1ABC free-text
5. AG6AQ -> K1ABC/P free-text
6. AG6AQ/P -> K1ABC/P ACK
7. <....> -> K1ABC ACK
8. AG6AQ -> K1ABC SNR with -12
9. AG6AQ -> K1ABC HEARTBEAT SNR with +05
10. a non-SNR command with nonzero numeric field
11. a special destination such as @ALLCALL for decode compatibility only

Generate from exact v3.0.3 formulas, not production MiniShell code. Pin Varicode.cpp/.h hashes and document provenance. Normal CTest must not require Qt/upstream.

## Primitive tests

Cover:

- all 32 command codes/names;
- all 64 number6 values;
- both portable flags independently and together;
- all 54 special basecalls;
- generic callsign decoding around nbasecall;
- 3D0 -> 3DA0 transform;
- Q[A-Z] -> 3X transform;
- <....> placeholder;
- DIRECTED class required;
- other application classes rejected;
- all eight transmission values leave content unchanged;
- invalid bits/null output preserve canaries.

## Host diagnostics

Extend js8_decode for DIRECTED frames only. Append stable fields such as:

    from=AG6AQ to=K1ABC cmd=" ACK"

and when present:

    num=-12

For free-text command append free_text=1. For ACK and 73 explicit flags ack=1 and end73=1 are acceptable.

Do not infer a complete chat message; following data frames are later work.

The real A_2_1 WAV is DATA_COMPRESSED and must remain unchanged.

## Documentation

Update docs/js8/application-protocol.md with standard directed layout, portable flags, command map, numeric semantics, and a note that decode support does not imply automatic action support.

## Architectural constraints

- pure C
- no heap or mutable global state
- no Qt/Boost
- no Huffman/JSC
- no MiniShell/platform
- no FT8
- no conversation/reassembly state
- no TX packer
- Normal only
- fixed bounded buffers
- exact v3.0.3 RX behavior

## Non-goals

Do NOT implement:

- COMPOUND_DIRECTED command semantics
- pairing <....> with preceding compound identity
- data/Huffman/JSC decode
- continuation text
- FIRST/LAST reassembly
- conversation history
- heartbeat ACK automation
- query/reply policy
- relay/store-forward behavior
- group-operation UI/policy
- TX directed packing
- scheduler/UI/logging
- live MiniShell integration
- other JS8 modes

## Acceptance criteria

- [x] 28-bit callsign unpack matches v3.0.3
- [x] complete special basecalls table decoded
- [x] /P from/to flags decoded
- [x] all 32 command codes map to exact canonical names
- [x] ACK code 14 explicit
- [x] 73 code 28 explicit
- [x] free-text code 31 explicit
- [x] numeric field follows extra6-31 semantics
- [x] SNR commands identified/formattable
- [x] <....> placeholder decoded
- [x] fixed upstream-derived vectors checked in
- [x] transmission flags remain independent
- [x] no command auto-policy introduced
- [x] T057/T056 behavior unchanged
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

    codex/T058-js8-directed

Codex:

1. read AGENTS.md, canonical JS8 docs, T057/T056, and exact v3.0.3 directed/callsign functions;
2. implement callsign28 primitive and complete special basecall table;
3. implement command and numeric primitives;
4. implement pure standard DIRECTED decoder;
5. add source-pinned independent vectors/tests;
6. update host diagnostics for DIRECTED only;
7. do not implement compound-directed association or data text;
8. run full existing JS8 gates;
9. set Status to REVIEW;
10. fill notes/vector provenance;
11. push one reviewable commit and return SHA;
12. no PR and no Actions wait.

## Codex implementation notes

### Implementation summary

Implemented pure standard DIRECTED RX parsing, callsign28 unpacking with all 54
special basecalls, exact canonical command names, portable flags, numeric presence
and value, explicit free-text/ACK/73/SNR classifications, and optional SNR formatting.
Host diagnostics append sender, destination, quoted command, optional number, and
free-text/ACK/73 markers only for DIRECTED frames. No task deviations.

### Files changed

- `apps/js8chat/src/js8_engine/js8_directed.[ch]`: pure fixed-buffer primitives and
  standard directed decoder; immutable special-name and command tables.
- `apps/js8chat/tools/js8_decode.c`: DIRECTED diagnostics only.
- `tests/js8_directed_oracle.py`, `tests/js8_directed_vectors.h`,
  `tests/js8_directed_vectors.md`: pinned independent vectors and provenance.
- `tests/js8_directed_test.c`: primitive/envelope tests and test-only PHY waveform
  fixture mode using the independently fixed application payloads.
- `tests/js8_directed_wav_test.py`: eleven synthetic host end-to-end regressions.
- `tests/js8_tests.cmake`, `CMakeLists.txt`: pure unit and host regression wiring;
  new source belongs to the existing no-heap checked library.
- `docs/js8/application-protocol.md`: layout, portable flags, command names,
  numeric semantics, and decode/action boundary.
- This task: REVIEW status and evidence.

### Invariants preserved

Every 28-bit callsign value is supported. Generic decoding preserves interior spaces,
trims the ends, applies the upstream 3D0/QA-Z expansions, and appends /P when flagged.
Special names return immediately without /P; the frame still preserves raw portable
flags and packed callsigns. Output capacity is 11 bytes, including NUL, sufficient
for the longest special name (`@RESERVE/0`) and expanded generic callsigns with /P.

All 75 bits are validated through the T056 envelope before output assignment.
Invalid input/class leaves caller output unchanged. Transmission flags are independent;
no CRC checking is added. Number6 zero means absent, while raw 31 is present zero
and raw 63 is +32. Leading command spaces are preserved, including code 31's one space.

No heap, mutable global state, platform dependencies, FT8 changes, application TX
packer, compound association, command policy, data codec, continuation/reassembly,
conversation state, auto-replies, or UI. T056/T057 source and existing tests remain
unchanged, as do T054 DSP and the T055 WAV/frontend.

### Golden-vector provenance

Inspected exact v3.0.3 `Varicode.cpp/.h`: `packCallsign`, `unpackCallsign`, complete
`basecalls` and `directed_cmds`, directed packing/unpacking, `unpackCmd`, and
`formatSNR`. The standard directed five-bit command is looked up directly;
`unpackCmd`'s alternate compound command/number byte is deliberately not used here.

The offline generator SHA-256 checks both sources, extracts every special name,
and sorts command keys to reproduce QMap::key canonical aliases. Its independent
Python formulas create the eleven required fixed frame vectors plus callsign
edge/transform fixtures; it does not call MiniShell code. Hashes, regeneration
commands, and vector details are in `tests/js8_directed_vectors.md`.

The non-SNR +32 fixture deliberately uses arbitrary RX wire value 63 rather than
upstream TX clamping. The host regression uses the existing PHY encoder only to
produce audio from fixed application payloads and checks independently generated
expected diagnostics. It is not used as an independent PHY golden oracle.
Normal CTest requires neither Qt nor an upstream checkout.

### Test evidence

All final commands passed on 2026-09-22:

```sh
cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure
# 99/99, external WAV not configured; includes boundary/no-heap and earlier JS8 tests.

cmake -S tests/unit -B /tmp/T058-build-unit
cmake --build /tmp/T058-build-unit -j"$(nproc)"
ctest --test-dir /tmp/T058-build-unit --output-on-failure
# 21/21.

PYTHONDONTWRITEBYTECODE=1 python3 tests/app_dependency_boundary.py . js8chat
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_platform_boundary.py . js8chat
# Both PASS.

cmake -S . -B /tmp/T058-build-ref -DJS8_A2_1_REFERENCE_WAV="$HOME/projects/js8chat/A_2_1.wav"
cmake --build /tmp/T058-build-ref -j"$(nproc)"
ctest --test-dir /tmp/T058-build-ref -R 'js8.*reference|js8.*A2.*1' --output-on-failure
# 1/1.
git hash-object ~/projects/js8chat/A_2_1.wav
# d986a4e5a9cc654dffbfadae73ec35cc9cea1d83
/tmp/T058-build-ref/js8_decode "$HOME/projects/js8chat/A_2_1.wav"

cmake -S . -B /tmp/T058-build-sanitize \
  -DCMAKE_C_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer -g' \
  -DJS8_A2_1_REFERENCE_WAV="$HOME/projects/js8chat/A_2_1.wav"
cmake --build /tmp/T058-build-sanitize -j"$(nproc)" \
  --target js8_rx_unit js8_phy_unit js8_frame_unit js8_protocol_frame_unit js8_compound_unit js8_directed_unit js8_decode
ctest --test-dir /tmp/T058-build-sanitize \
  -R '^js8_(phy_unit|rx_unit|frame_unit|protocol_frame_unit|compound_unit|directed_unit|wav_unit|directed_wav_unit|A2_1_reference)$' \
  --output-on-failure
# 9/9; executed outside sandbox for LeakSanitizer ptrace compatibility.

source ~/projects/esp-idf/export.sh
idf.py -C platform/adv build
# PASS; image 0x151790 bytes, app partition 78% free.

python3 tests/js8_directed_oracle.py /tmp/T056-Varicode.cpp /tmp/T056-Varicode.h > /tmp/T058-regenerated.h
cmp tests/js8_directed_vectors.h /tmp/T058-regenerated.h
# Identical.
git diff --check
# PASS.
```

Coverage includes all 32 commands x 64 numeric values x four portable combinations,
all 54 special names with both portable states, generic boundaries and transforms,
all eight transmission values per fixed frame, every invalid bit value 2..255 at
every position, null/class rejection with unchanged canaries, and the complete
SNR formatter range with out-of-range extremes. Eleven synthetic WAVs each decode
to exactly one expected payload and stable host diagnostics.

Exact external WAV stdout (byte-for-byte identical to saved T057 output):

```text
payload=111001011101001010000111001011100000101011000001100010000111111111111111010 type=2 frame="vTA7BWh1Y7++" tx_raw=2 class=data_compressed tx=LAST score=26 time=5/0 freq=57/0 hz=556.250 hard_errors=15
```

The decode summary remains `blocks=93 ignored_engine_samples=720 candidates=50
ldpc_fail=49 crc_fail=0 valid=1 unique=1`. The WAV remains external, uncommitted.

### Manual validation still required

None for T058; no hardware/RF acceptance required.

### Known limitations / risks

Callsigns and special/group names are decoded for wire compatibility without
validity filtering or operation policy. `<....>` remains unresolved. No following
message text or compound-directed association is inferred. Caller supplies a
validated PHY payload; content parsing does not repeat CRC validation.

### Commit

One reviewable commit on `codex/T058-js8-directed` containing these notes; its SHA
is returned after push. No PR or GitHub Actions wait.

## Supervisor review

## Architect test result

No hardware/RF acceptance required.

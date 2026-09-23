# T057 — JS8 heartbeat/CQ and compound identity decoder

Status: REVIEW

## Architect intent

Start decoding useful JS8 application content after T056 established the correct protocol-envelope layering.

Keep this stage small and practical:

- decode who transmitted a heartbeat or CQ;
- decode its optional 4-character grid;
- distinguish HB from CQ and identify the CQ variant, including CQ FIELD;
- decode plain compound callsign/grid identity frames;
- expose the shared raw compound fields needed by later compound-directed work.

Do not implement directed commands, data text, Huffman/JSC, reassembly, conversations, or UI yet.

JS8Chat remains Normal/Mode A only.

## Objective

Implement pure RX decoding for the v3.0.3 shared compound-frame layout used by FrameHeartbeat, FrameCompound, and FrameCompoundDirected.

The shared 72 application bits are:

    bits 0..2   application class
    bits 3..52  callsign50
    bits 53..68 extra16
    bits 69..71 bits3

T057 must expose the common fields, then add semantic wrappers for heartbeat/CQ and plain compound identity.

## Accepted baseline

T052-T055: first Linux WAV/PHY milestone COMPLETE.
T056: protocol envelope/tx-flag separation COMPLETE.

Read before editing:

    AGENTS.md
    docs/js8/architecture.md
    docs/js8/application-protocol.md
    docs/project/codex/T056-js8-protocol-envelope.md

    apps/js8chat/src/js8_engine/js8_protocol_frame.[ch]
    apps/js8chat/src/js8_engine/js8_frame.[ch]

Frozen reference:

    JS8Call-improved/JS8Call-improved
    tag v3.0.3

Normative source:

    JS8_Main/Varicode.cpp
    JS8_Main/Varicode.h

Important upstream functions/constants:

    packAlphaNumeric50 / unpackAlphaNumeric50
    packGrid / unpackGrid
    packCompoundFrame / unpackCompoundFrame
    packHeartbeatMessage / unpackHeartbeatMessage
    packCompoundMessage / unpackCompoundMessage
    cqs
    hbs
    nbasegrid = 32400
    nusergrid = 32410
    nmaxgrid = 32767

## Shared callsign50 decoding

The v3.0.3 alphanumeric alphabet is:

    0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ /@

The packed 50-bit mixed-radix representation is:

    [39][38][38][2][38][38][38][2][38][38][38]

The two radix-2 positions encode slash-vs-space separators.

Unpack in the exact upstream order and remove spaces from the final 11-character representation, matching unpackAlphaNumeric50.

Do not add validity rejection that upstream does not perform.

Output storage must allow 11 visible characters plus NUL.

## Grid decoding

The low 15 bits of a heartbeat extra field and valid plain-compound extras use upstream 15-bit Maidenhead packing.

For unpacking:

    value > 32400 -> no grid
    otherwise reproduce v3.0.3 unpackGrid() and return 4 characters

Use fixed output storage of five chars including NUL.

Do not generalize to 6/8/10-character grids.

## Raw compound-field decoder

Add a low-level pure decoder that accepts only application classes HEARTBEAT, COMPOUND, and COMPOUND_DIRECTED and returns equivalent fields:

    app_class
    callsign[12]
    extra16
    bits3

Suggested type:

    typedef struct {
        Js8AppFrameClass app_class;
        char callsign[12];
        uint16_t extra16;
        uint8_t bits3;
    } Js8CompoundFields;

Suggested API:

    int js8_compound_fields_decode(
        const uint8_t payload_bits[JS8_PAYLOAD_BITS],
        Js8CompoundFields *out);

It must validate all input bits, use T056 envelope classification, reject DIRECTED/DATA/DATA_COMPRESSED classes, decode callsign50/extra16/bits3 exactly, ignore final transmission flags except for input validation, and leave output unchanged on invalid input.

This raw decoder is the foundation for later compound-directed command decoding.

## Heartbeat / CQ semantic decoder

Upstream FrameHeartbeat carries both HB and CQ.

extra16 layout:

    bit 15    0 = heartbeat
              1 = CQ family
    bits 0..14 packed grid

bits3 selects the subtype.

For heartbeat, all subtype values 0..7 map to HB in v3.0.3.

For CQ, exact mapping is:

    0 -> CQ CQ CQ
    1 -> CQ DX
    2 -> CQ QRP
    3 -> CQ CONTEST
    4 -> CQ FIELD
    5 -> CQ FD
    6 -> CQ CQ
    7 -> CQ

Implement a pure semantic output equivalent to:

    typedef struct {
        char callsign[12];
        char grid[5];
        int has_grid;
        int is_cq;
        uint8_t subtype;
    } Js8BeaconFrame;

Provide stable fixed-string helpers for CQ/HB subtype naming.

A beacon decoder accepts only HEARTBEAT application frames.

For no-grid sentinel/reserved values, return has_grid=0 and an empty grid.

CQ FIELD here is simply the frozen portable calling variant; it is not @POTA/@SOTA group operation.

## Plain compound identity decoder

For application class COMPOUND:

- decode callsign50;
- if extra16 <= 32400, decode 4-character grid and set has_grid=1;
- otherwise preserve raw extra16 and report no grid;
- expose bits3 raw for diagnostics.

Suggested output:

    typedef struct {
        char callsign[12];
        char grid[5];
        int has_grid;
        uint16_t extra16;
        uint8_t bits3;
    } Js8CompoundIdentity;

Do not interpret the 32410..32766 command range in T057.

## Compound-directed handling in T057

The low-level shared decoder MUST accept COMPOUND_DIRECTED and return raw callsign, extra16, and bits3.

T057 MUST NOT translate extra16 into ACK, 73, SNR, or any other command.

## Golden vectors

Add fixed v3.0.3-derived vectors for at least:

1. heartbeat: AG6AQ, CM97, HB subtype 0
2. CQ FIELD: AG6AQ, CM97, CQ subtype 4
3. CQ: KN4CRD, no grid, CQ subtype 7
4. plain compound: KN4CRD/P, EM73
5. plain compound without grid: valid compound callsign, extra16=32767
6. one COMPOUND_DIRECTED raw frame: verify callsign, exact extra16, bits3 only

Vectors must be derived from exact v3.0.3 formulas and checked in. Document provenance. A small offline source-pinned Python generator is preferred if useful. It must not call MiniShell production code.

Normal CTest must not require Qt or an upstream checkout.

## Tests

Pure tests must cover:

- mixed-radix callsign separator positions;
- first-character radix-39 behavior including @;
- space stripping like upstream;
- representative 50-bit edge values;
- grid values including 0, normal locators, 32400, 32401, 32767;
- shared raw extraction for HEARTBEAT/COMPOUND/COMPOUND_DIRECTED;
- rejection of DIRECTED/DATA/DATA_COMPRESSED by raw compound decoder;
- all eight CQ subtype names;
- all eight heartbeat subtype values normalize to HB;
- heartbeat/CQ grid and no-grid behavior;
- plain compound grid and no-grid behavior;
- invalid bits/null arguments leave outputs unchanged;
- FIRST/LAST transmission flags do not change content decoding.

Keep existing T056 envelope tests unchanged.

## Host diagnostic integration

Extend js8_decode only for classes this task can semantically decode.

For HEARTBEAT frames append stable diagnostics such as:

    call=AG6AQ beacon=HB grid=CM97

or:

    call=AG6AQ beacon="CQ FIELD" grid=CM97

For COMPOUND frames append:

    call=KN4CRD/P grid=EM73

For COMPOUND_DIRECTED it is acceptable to append only raw shared fields:

    call=... extra=... bits3=...

because command semantics are deferred.

For DATA/DATA_COMPRESSED/DIRECTED, existing T056 output remains unchanged.

The pinned A_2_1 WAV is DATA_COMPRESSED, so its reference line should remain semantically unchanged.

## Documentation

Update docs/js8/application-protocol.md with:

- shared compound bit layout;
- callsign50 ownership;
- heartbeat/CQ extra16 layout;
- CQ subtype mapping.

Do not change frozen product scope.

## Architectural constraints

- pure C;
- no heap;
- no mutable global state;
- no Qt/Boost;
- no JSC/Huffman;
- no MiniShell/platform;
- no FT8;
- no conversation/reassembly state;
- no TX packing;
- Normal only;
- fixed caller-owned buffers;
- exact v3.0.3 semantics over clever abstractions.

## Non-goals

Do NOT implement:

- standard 28-bit directed callsign packing;
- directed commands;
- ACK/73 semantics;
- SNR numeric commands;
- FIRST/LAST reassembly;
- Huffman/JSC data;
- free-text continuation;
- TX heartbeat/CQ packing;
- station activity tables;
- automatic heartbeat ACK;
- scheduler;
- UI/logging;
- live MiniShell integration;
- other JS8 speeds.

## Acceptance criteria

- [x] shared compound fields decoded exactly
- [x] callsign50 mixed-radix unpack matches v3.0.3
- [x] 15-bit 4-character grid unpack matches v3.0.3
- [x] heartbeat vs CQ alt bit decoded correctly
- [x] all eight CQ subtype mappings exact
- [x] all HB subtype values normalize to HB
- [x] CQ FIELD decoded explicitly
- [x] plain compound callsign/grid decoded
- [x] COMPOUND_DIRECTED raw fields available without command interpretation
- [x] fixed upstream-derived vectors checked in
- [x] T056 envelope semantics unchanged
- [x] T054/T055 DSP/WAV behavior unchanged
- [x] js8_engine remains pure/no-heap
- [x] no FT8 changes
- [x] Linux full CTest passes
- [x] portable CTest passes
- [x] optional A_2_1 regression passes
- [x] boundary/no-heap tests pass
- [x] ASan/UBSan passes
- [x] ADV build remains green
- [x] git diff --check passes
- [x] no unrelated cleanup

No hardware/RF validation is required.

## Branch workflow

Use:

    codex/T057-js8-beacon-compound

Codex:

1. read AGENTS.md, canonical JS8 protocol docs, T056, and exact v3.0.3 Varicode functions;
2. implement the shared compound-field primitive first;
3. implement heartbeat/CQ and plain compound semantic wrappers;
4. add fixed upstream-derived vectors and primitive tests;
5. do not decode compound-directed commands;
6. update host diagnostics only where semantics are known;
7. run the full T056/T055 gate set;
8. set Status to REVIEW;
9. fill implementation notes and vector provenance;
10. commit and push one reviewable commit;
11. return SHA;
12. no PR and no Actions wait.

## Codex implementation notes

### Implementation summary

Added pure `js8_compound` callsign50/grid primitives, shared compound extraction,
heartbeat/CQ semantics, plain compound identity, and fixed subtype names.
The shared decoder uses the T056 envelope to validate all bits and restrict classes.
COMPOUND_DIRECTED exposes raw fields only. Host output appends known content after
the existing diagnostics; unsupported content classes retain their original output.
No task deviations.

### Files changed

- `apps/js8chat/src/js8_engine/js8_compound.[ch]`: fixed-buffer pure RX decoders.
- `apps/js8chat/tools/js8_decode.c`: heartbeat, compound, and raw compound-directed diagnostics.
- `tests/js8_compound_test.c`: primitives, fixed frames, subtype/grid combinations,
  all transmission values, class rejection, null pointers, every invalid bit byte
  value at every payload position, and unchanged outputs on rejection.
- `tests/js8_compound_oracle.py`, `tests/js8_compound_vectors.h`,
  `tests/js8_compound_vectors.md`: source-pinned offline vectors and provenance.
- `tests/js8_tests.cmake`: compile module into the no-heap checked library and run
  the new unit test in Linux and portable builds.
- `tests/js8_wav_test.py`: existing independent T053 tones now check all three
  supported host dispatch classes, including raw-only compound-directed output.
- `docs/js8/application-protocol.md`: shared bit layout, callsign ownership, grid
  boundary, HB/CQ alternate bit and subtype mapping.
- This task: status and review evidence.

### Invariants preserved

Pure C, no heap, no mutable global state, fixed caller-owned buffers. Invalid
input leaves outputs unchanged. No additional callsign validity filtering; spaces
are stripped everywhere, and first-character radix 39 includes `@`. Values within
50 bits use upstream modulo behavior even above the mixed-radix product.
Grid 32400 decodes to RA90; only values greater than 32400 mean no grid.
Transmission flags never affect content interpretation. All extras above 32400
remain uninterpreted in plain compound identity, including the command range.

T056 envelope source/tests, T054 DSP, T055 WAV/frontend, and FT8 are unchanged.
No command, ACK/73, SNR, Huffman/JSC, reassembly, application state, TX, or UI work.
Frozen product scope remains unchanged.

### Golden-vector provenance

Read exact v3.0.3 `JS8_Main/Varicode.cpp/.h`, including alpha50 pack/unpack,
grid conversion/pack/unpack, compound pack/unpack, heartbeat pack/unpack, and
`cqs`/`hbs`. Source SHA-256 values and exact regeneration instructions are in
`tests/js8_compound_vectors.md`.

The generator verifies source hashes, transcribes upstream formulas, and never
calls MiniShell code. Six checked-in frame vectors cover AG6AQ HB/CM97 and CQ
FIELD/CM97, KN4CRD CQ/no-grid, KN4CRD/P compound/EM73, VE3/LB9YHX compound/no-grid,
and KN4CRD/P compound-directed raw extra=32442/bits3=6. Separate primitive vectors
cover slash positions, `@`, stripped spaces, empty output, 11 visible characters,
50-bit edges, and grid boundaries. Regeneration is byte-for-byte reproducible;
normal CTest requires no Qt or upstream checkout.

### Test evidence

Final commands passed on 2026-09-22:

```sh
cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure
# 97/97, without external WAV; includes boundary/no-heap and unchanged T056 tests.

cmake -S tests/unit -B /tmp/T057-build-unit
cmake --build /tmp/T057-build-unit -j"$(nproc)"
ctest --test-dir /tmp/T057-build-unit --output-on-failure
# 20/20.

PYTHONDONTWRITEBYTECODE=1 python3 tests/app_dependency_boundary.py . js8chat
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_platform_boundary.py . js8chat
# Both PASS.

cmake -S . -B /tmp/T057-build-ref -DJS8_A2_1_REFERENCE_WAV="$HOME/projects/js8chat/A_2_1.wav"
cmake --build /tmp/T057-build-ref -j"$(nproc)"
ctest --test-dir /tmp/T057-build-ref -R 'js8.*reference|js8.*A2.*1' --output-on-failure
# 1/1.
git hash-object ~/projects/js8chat/A_2_1.wav
# d986a4e5a9cc654dffbfadae73ec35cc9cea1d83
/tmp/T057-build-ref/js8_decode "$HOME/projects/js8chat/A_2_1.wav"

cmake -S . -B /tmp/T057-build-sanitize \
  -DCMAKE_C_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer -g' \
  -DJS8_A2_1_REFERENCE_WAV="$HOME/projects/js8chat/A_2_1.wav"
cmake --build /tmp/T057-build-sanitize -j"$(nproc)" \
  --target js8_rx_unit js8_phy_unit js8_frame_unit js8_protocol_frame_unit js8_compound_unit js8_decode
ctest --test-dir /tmp/T057-build-sanitize \
  -R '^js8_(phy_unit|rx_unit|frame_unit|protocol_frame_unit|compound_unit|wav_unit|A2_1_reference)$' \
  --output-on-failure
# 7/7; run outside sandbox for LeakSanitizer ptrace compatibility.

source ~/projects/esp-idf/export.sh
idf.py -C platform/adv build
# PASS; image 0x151790 bytes, 78% of app partition free.

python3 tests/js8_compound_oracle.py /tmp/T056-Varicode.cpp /tmp/T056-Varicode.h > /tmp/T057-regenerated.h
cmp tests/js8_compound_vectors.h /tmp/T057-regenerated.h
# Identical.
git diff --check
# PASS.
```

An initial `-Werror=sign-compare` in the new test assertion was corrected by
explicitly converting the boolean field for its unsigned expected value; the
final full builds and tests above pass.

Exact real-WAV stdout (also compared byte-for-byte with saved T056 output):

```text
payload=111001011101001010000111001011100000101011000001100010000111111111111111010 type=2 frame="vTA7BWh1Y7++" tx_raw=2 class=data_compressed tx=LAST score=26 time=5/0 freq=57/0 hz=556.250 hard_errors=15
```

Decode summary remains:

```text
blocks=93 ignored_engine_samples=720 candidates=50 ldpc_fail=49 crc_fail=0 valid=1 unique=1
```

The WAV remains external and is not committed.

### Manual validation still required

None for T057; no hardware/RF acceptance required.

### Known limitations / risks

These content decoders assume an already validated PHY payload; they do not perform
CRC checking. Unconventional or empty callsigns are accepted as upstream specifies.
COMPOUND_DIRECTED and plain compound command-range extras have no command semantics.
No data-text decoder or frame reassembly is implemented.

### Commit

One reviewable commit on `codex/T057-js8-beacon-compound` containing these notes;
the SHA is returned after push. No PR or Actions wait.

## Supervisor review

## Architect test result

No hardware/RF acceptance required.

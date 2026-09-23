# T056 — JS8 protocol envelope and transmission flags

Status: COMPLETE

## Architect intent

Continue JS8Chat after the completed real-WAV PHY milestone, while keeping the application protocol layered and KISS.

T055 proved a 75-bit PHY payload can be recovered from the upstream A_2_1 WAV. The next step is to interpret only the JS8 protocol envelope, not yet full message contents.

Critical distinction:

    75-bit PHY payload
      = 72 application-frame bits
      + 3 transmission bits

The final 3 bits are NOT the application frame class.

For the T055 fixture:

    physical frame = vTA7BWh1Y7++
    transmission bits = 010

010 means JS8CallLast. It does not mean FrameCompoundDirected.

## Objective

Implement a small pure JS8 protocol-envelope module that:

1. classifies the application frame family from the first bits of the 72-bit application payload;
2. decodes the final 3-bit transmission field as FIRST/LAST/DATA flags;
3. exposes these facts without attempting callsign, command, Huffman, JSC, or conversation semantics;
4. updates Linux js8_decode output so the real T055 fixture is clearly reported as its actual application frame family plus transmission flags.

## Accepted baseline

T052 through T055 are COMPLETE.

First real WAV payload:

    111001011101001010000111001011100000101011000001100010000111111111111111010

Physical text12:

    vTA7BWh1Y7++

Tail transmission field:

    010

## Source of truth

Read before editing:

    AGENTS.md
    docs/js8/architecture.md
    docs/js8/application-protocol.md
    docs/js8/implementation-plan.md
    docs/project/codex/T055-js8-wav-decode.md

    apps/js8chat/src/js8_engine/js8_frame.[ch]
    apps/js8chat/tools/js8_decode.c

Frozen interoperability reference:

    JS8Call-improved/JS8Call-improved
    tag v3.0.3

Normative files:

    JS8_Main/Varicode.h
    JS8_Main/Varicode.cpp
    JS8_Mode/DecodedText.cpp

## Upstream layer definitions

### Transmission field

v3.0.3 Varicode::TransmissionType:

    JS8Call      = 0  // 000
    JS8CallFirst = 1  // 001
    JS8CallLast  = 2  // 010
    JS8CallData  = 4  // 100

These are bit flags and may be ORed.

Therefore the final 3 PHY bits are decoded as:

    value 1 -> FIRST
    value 2 -> LAST
    value 4 -> DATA

For Normal-mode legacy data frames, the application data class is encoded inside the first 72 bits. DATA=4 in the transmission field must not be confused with FrameData.

JS8Chat v0.1 remains Normal only. T056 may preserve/report the raw DATA flag but must not add another JS8 mode.

### Application frame class

v3.0.3 Varicode::FrameType:

    000 -> FrameHeartbeat
    001 -> FrameCompound
    010 -> FrameCompoundDirected
    011 -> FrameDirected
    10X -> FrameData
    11X -> FrameDataCompressed

For data classes the third prefix bit is payload, not a distinct frame class. Normalize:

    100, 101 -> FrameData
    110, 111 -> FrameDataCompressed

Use explicit JS8Chat-owned enum names; do not import Qt/upstream headers.

## Architectural constraints

- Pure C.
- No heap.
- No mutable global state.
- No Qt, Boost, Huffman, or JSC dependency.
- No MiniShell/platform dependency.
- No FT8 dependency.
- Normal mode only.
- Do not interpret callsigns, grids, commands, CQ/HB meaning, Huffman, or JSC yet.
- Do not create conversation or reassembly state.
- Keep physical-frame unpacking in js8_frame separate from application-envelope classification.
- Do not alter T054 DSP or T055 WAV/frontend behavior.
- Existing exact 75-bit payload remains the authoritative identity.

## Implementation scope

Add a pure module under apps/js8chat/src/js8_engine/, for example:

    js8_protocol_frame.c
    js8_protocol_frame.h

Suggested types:

    typedef enum {
        JS8_APP_FRAME_HEARTBEAT = 0,
        JS8_APP_FRAME_COMPOUND,
        JS8_APP_FRAME_COMPOUND_DIRECTED,
        JS8_APP_FRAME_DIRECTED,
        JS8_APP_FRAME_DATA,
        JS8_APP_FRAME_DATA_COMPRESSED
    } Js8AppFrameClass;

    enum {
        JS8_TX_FLAG_FIRST = 1u,
        JS8_TX_FLAG_LAST  = 2u,
        JS8_TX_FLAG_DATA  = 4u
    };

    typedef struct {
        Js8AppFrameClass app_class;
        uint8_t raw_app_prefix3;
        uint8_t tx_flags;
        int first;
        int last;
        int data_flag;
    } Js8ProtocolEnvelope;

A suitable API is:

    int js8_protocol_envelope_decode(
        const uint8_t payload_bits[JS8_PAYLOAD_BITS],
        Js8ProtocolEnvelope *out);

Exact naming may differ, but preserve the layer semantics.

Invalid bits/pointers must fail without modifying output.

## Application-prefix tests

Exhaustively test all 8 possible first-three-bit prefixes:

    000 -> HEARTBEAT
    001 -> COMPOUND
    010 -> COMPOUND_DIRECTED
    011 -> DIRECTED
    100 -> DATA
    101 -> DATA
    110 -> DATA_COMPRESSED
    111 -> DATA_COMPRESSED

Do not infer class from the physical 12-character text.

## Transmission-flag tests

Exhaustively test all 8 final 3-bit values:

    000 -> none
    001 -> FIRST
    010 -> LAST
    011 -> FIRST|LAST
    100 -> DATA
    101 -> DATA|FIRST
    110 -> DATA|LAST
    111 -> DATA|FIRST|LAST

The raw value must remain available for diagnostics.

## T055 real-WAV regression

The accepted T055 payload begins with 111 and ends with 010.

Therefore T056 must classify it as:

    application class = DATA_COMPRESSED
    transmission      = LAST

This is a hard regression.

Update js8_decode stable output to append fields such as:

    class=data_compressed tx=LAST

while retaining the existing payload and frame fields.

For compatibility, type=2 may remain temporarily, but documentation/comments must state that it is the raw transmission field. Prefer also printing tx_raw=2.

A reasonable stable line is:

    payload=... type=2 tx_raw=2 class=data_compressed tx=LAST frame="vTA7BWh1Y7++" ...

Exact spelling may differ but must be stable and tested.

## Optional naming helpers

Pure helpers returning fixed string literals are acceptable:

    heartbeat
    compound
    compound_directed
    directed
    data
    data_compressed

A bounded transmission-flag formatter is also acceptable.

Do not introduce heap/dynamic strings merely for diagnostics.

## Tests

Add focused pure tests for:

- all 8 application prefixes;
- all 8 transmission flag combinations;
- T055 pinned payload -> DATA_COMPRESSED + LAST;
- existing T052/T053 golden payloads still classify deterministically;
- invalid bits and null arguments leave output unchanged;
- no heap/boundary regressions.

Update the optional A_2_1 external reference test to require the classification fields.

Normal CTest must still run without the external WAV.

## Documentation correction

Update docs/js8/implementation-plan.md to mark the first Linux WAV milestone complete.

Add a short protocol-layer note to docs/js8/application-protocol.md clarifying:

    physical transmission bits != application FrameType

and record the exact upstream mappings above.

Do not change the frozen v0.1 product scope.

## Non-goals

Do not implement in T056:

- heartbeat callsign/grid unpacking;
- CQ/CQ FIELD meaning;
- compound callsign unpacking;
- directed from/to/cmd unpacking;
- ACK/73 semantics;
- FIRST/LAST reassembly policy;
- Huffman decode;
- JSC decode;
- text continuation;
- TX protocol packing;
- conversation state;
- scheduler;
- UI;
- live MiniShell integration;
- other JS8 modes.

## Acceptance criteria

- [x] application class and transmission field are separate concepts
- [x] all 8 application-prefix values classify exactly like v3.0.3
- [x] all 8 transmission values decode as bit flags
- [x] T055 fixture classifies as DATA_COMPRESSED + LAST
- [x] js8_decode prints stable class/tx diagnostics
- [x] T054 DSP and T055 WAV/frontend behavior unchanged
- [x] js8_engine remains pure/no-heap
- [x] no FT8 changes
- [x] normal Linux CTest passes
- [x] portable CTest passes
- [x] optional A_2_1 reference regression passes
- [x] boundary/no-heap tests pass
- [x] ASan/UBSan passes
- [x] ADV build remains green
- [x] git diff --check passes
- [x] no unrelated cleanup

No manual hardware/RF validation is required.

## Branch workflow

Use:

    codex/T056-js8-protocol-envelope

Codex:

1. read AGENTS.md, canonical JS8 docs, T055, and this task;
2. inspect exact v3.0.3 TransmissionType, FrameType, and DecodedText dispatch;
3. implement only the pure protocol envelope/classifier;
4. update host diagnostics/reference tests;
5. make the protocol-layer documentation correction;
6. run all normal gates used by T055;
7. set Status to REVIEW;
8. record implementation/test evidence;
9. commit and push one reviewable commit;
10. return SHA;
11. no PR and no Actions wait.

## Codex implementation notes

### Implementation summary

Added a pure Normal protocol-envelope classifier with independent application-prefix
and transmission-flag fields. Fixed literal naming helpers feed stable host diagnostics;
legacy `type=` remains the raw transmission field, also printed as `tx_raw=`.
No deviations from task scope.

Inspected v3.0.3 `JS8_Main/Varicode.h`, `JS8_Main/Varicode.cpp`, and
`JS8_Mode/DecodedText.cpp`. Enum values and Normal data header handling establish
000/001/010/011/10X/11X classes independently of FIRST=1, LAST=2, DATA=4.
`DecodedText` selects an alternate unpacker for DATA; this classifier only reports
that flag and the Normal prefix, as required, without adding that unpacker.

### Files changed

- `apps/js8chat/src/js8_engine/js8_protocol_frame.[ch]`: validated pure envelope and names.
- `apps/js8chat/src/js8_engine/js8_frame.h`: clarify the existing physical type field.
- `apps/js8chat/tools/js8_decode.c`: append independent envelope diagnostics.
- `tests/js8_protocol_frame_test.c`, `tests/js8_tests.cmake`: exhaustive prefix/flag
  cross-product with zero/one middle bits, all existing golden payloads, pinned WAV
  payload, invalid bits at every position, null arguments, and naming checks.
- `tests/js8_wav_test.py`, `tests/js8_wav_reference.py`: require stable classification.
- `docs/js8/application-protocol.md`, `docs/js8/implementation-plan.md`: layer mapping
  and completed first Linux WAV milestone; frozen product scope unchanged.
- This task: review handoff and evidence.

### Invariants preserved

All 75 bits are validated before output writes. Input stays unchanged, invalid input
leaves output unchanged, and the module has no heap or mutable global state.
Physical unpacking remains separate. T054 DSP, T055 WAV/frontend, and FT8 code are
unchanged. No codecs, callsign/command interpretation, state, reassembly, or UI added.

### Test evidence

All commands passed on 2026-09-22:

```sh
cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure
# 96/96, including no-heap and boundaries; external WAV not configured.

cmake -S tests/unit -B /tmp/T056-build-unit
cmake --build /tmp/T056-build-unit -j"$(nproc)"
ctest --test-dir /tmp/T056-build-unit --output-on-failure
# 19/19.

PYTHONDONTWRITEBYTECODE=1 python3 tests/app_dependency_boundary.py . js8chat
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_platform_boundary.py . js8chat
# Both PASS.

cmake -S . -B /tmp/T056-build-ref -DJS8_A2_1_REFERENCE_WAV="$HOME/projects/js8chat/A_2_1.wav"
cmake --build /tmp/T056-build-ref -j"$(nproc)"
ctest --test-dir /tmp/T056-build-ref -R 'js8.*reference|js8.*A2.*1' --output-on-failure
# 1/1.

git hash-object ~/projects/js8chat/A_2_1.wav
# d986a4e5a9cc654dffbfadae73ec35cc9cea1d83
/tmp/T056-build-ref/js8_decode "$HOME/projects/js8chat/A_2_1.wav"

cmake -S . -B /tmp/T056-build-sanitize \
  -DCMAKE_C_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer -g' \
  -DJS8_A2_1_REFERENCE_WAV="$HOME/projects/js8chat/A_2_1.wav"
cmake --build /tmp/T056-build-sanitize -j"$(nproc)" \
  --target js8_rx_unit js8_phy_unit js8_frame_unit js8_protocol_frame_unit js8_decode
ctest --test-dir /tmp/T056-build-sanitize \
  -R '^js8_(phy_unit|rx_unit|frame_unit|protocol_frame_unit|wav_unit|A2_1_reference)$' \
  --output-on-failure
# 6/6; executed outside sandbox for LeakSanitizer ptrace compatibility.

source ~/projects/esp-idf/export.sh
idf.py -C platform/adv build
# PASS; image 0x151790 bytes, app partition 78% free.

git diff --check
# PASS.
```

Exact real-WAV stdout:

```text
payload=111001011101001010000111001011100000101011000001100010000111111111111111010 type=2 frame="vTA7BWh1Y7++" tx_raw=2 class=data_compressed tx=LAST score=26 time=5/0 freq=57/0 hz=556.250 hard_errors=15
```

Exact real-WAV stderr:

```text
candidate=0 score=26 time=5/0 freq=57/0 status=0
candidate=1 score=14 time=5/0 freq=57/1 status=-2
candidate=2 score=12 time=18/0 freq=54/0 status=-2
candidate=3 score=10 time=5/0 freq=56/1 status=-2
candidate=4 score=10 time=17/0 freq=110/1 status=-2
blocks=93 ignored_engine_samples=720 candidates=50 ldpc_fail=49 crc_fail=0 valid=1 unique=1
```

The payload, physical frame, candidate diagnostics and unique count match T055;
only envelope diagnostics were appended. The external WAV remains untracked outside
the repository and is not needed for normal CTest.

### Manual validation still required

None for T056; no hardware/RF acceptance required.

### Known limitations / risks

Envelope classification does not validate application contents or CRC and cannot
recover message text. DATA is reported without implementing the alternate unpacker.
FIRST/LAST are flags only; no reassembly policy is implied.

### Commit

The reviewed implementation commit is:

`9aee8aad9a4fe91a3a17b07b8c27a1a858fb59c3`

No PR or GitHub Actions wait.

## Supervisor review

Reviewed commit `9aee8aad9a4fe91a3a17b07b8c27a1a858fb59c3` against T056 and the pinned JS8Call-improved v3.0.3 protocol enums/dispatch.

Result: **PASS**.

Review findings:

- One bounded implementation commit, one commit ahead of the T056 baseline.
- Application frame class and the tail transmission field are represented by separate types/fields.
- Exhaustive tests cover all eight application prefixes and all eight transmission flag combinations.
- Prefix normalization matches v3.0.3 exactly: 000 heartbeat, 001 compound, 010 compound-directed, 011 directed, 10X data, 11X compressed data.
- FIRST=1, LAST=2, DATA=4 are treated as independent bit flags.
- The accepted T055 payload classifies as DATA_COMPRESSED + LAST, correcting the prior ambiguity around `type=2`.
- `js8_decode` retains the original payload/frame diagnostics while appending stable envelope diagnostics.
- Physical-frame unpacking remains separate from protocol-envelope classification.
- No callsign, command, Huffman, JSC, reassembly, conversation, or UI semantics were introduced.
- T054 DSP and T055 WAV/frontend product source are unchanged; FT8 is unchanged.
- Documentation now explicitly records that physical transmission bits are not application FrameType and marks T052-T055 milestone 1 complete.
- Reported gates are consistent with the diff: Linux 96/96, portable 19/19, reference WAV, sanitizer 6/6, boundary/no-heap checks, ADV build, and diff check all pass.

Main was fast-forwarded to the reviewed implementation commit.

## Architect test result

No hardware/RF acceptance required. Software review accepted; task complete.

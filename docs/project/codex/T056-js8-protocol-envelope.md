# T056 — JS8 protocol envelope and transmission flags

Status: READY

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

- [ ] application class and transmission field are separate concepts
- [ ] all 8 application-prefix values classify exactly like v3.0.3
- [ ] all 8 transmission values decode as bit flags
- [ ] T055 fixture classifies as DATA_COMPRESSED + LAST
- [ ] js8_decode prints stable class/tx diagnostics
- [ ] T054 DSP and T055 WAV/frontend behavior unchanged
- [ ] js8_engine remains pure/no-heap
- [ ] no FT8 changes
- [ ] normal Linux CTest passes
- [ ] portable CTest passes
- [ ] optional A_2_1 reference regression passes
- [ ] boundary/no-heap tests pass
- [ ] ASan/UBSan passes
- [ ] ADV build remains green
- [ ] git diff --check passes
- [ ] no unrelated cleanup

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

### Files changed

### Invariants preserved

### Test evidence

### Manual validation still required

### Known limitations / risks

### Commit

## Supervisor review

## Architect test result

No hardware/RF acceptance required.

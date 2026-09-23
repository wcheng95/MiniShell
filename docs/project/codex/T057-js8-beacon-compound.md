# T057 — JS8 heartbeat/CQ and compound identity decoder

Status: READY

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

- [ ] shared compound fields decoded exactly
- [ ] callsign50 mixed-radix unpack matches v3.0.3
- [ ] 15-bit 4-character grid unpack matches v3.0.3
- [ ] heartbeat vs CQ alt bit decoded correctly
- [ ] all eight CQ subtype mappings exact
- [ ] all HB subtype values normalize to HB
- [ ] CQ FIELD decoded explicitly
- [ ] plain compound callsign/grid decoded
- [ ] COMPOUND_DIRECTED raw fields available without command interpretation
- [ ] fixed upstream-derived vectors checked in
- [ ] T056 envelope semantics unchanged
- [ ] T054/T055 DSP/WAV behavior unchanged
- [ ] js8_engine remains pure/no-heap
- [ ] no FT8 changes
- [ ] Linux full CTest passes
- [ ] portable CTest passes
- [ ] optional A_2_1 regression passes
- [ ] boundary/no-heap tests pass
- [ ] ASan/UBSan passes
- [ ] ADV build remains green
- [ ] git diff --check passes
- [ ] no unrelated cleanup

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

### Files changed

### Invariants preserved

### Golden-vector provenance

### Test evidence

### Manual validation still required

### Known limitations / risks

### Commit

## Supervisor review

## Architect test result

No hardware/RF acceptance required.

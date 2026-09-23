# T058 — JS8 standard directed frame decoder

Status: READY

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

- [ ] 28-bit callsign unpack matches v3.0.3
- [ ] complete special basecalls table decoded
- [ ] /P from/to flags decoded
- [ ] all 32 command codes map to exact canonical names
- [ ] ACK code 14 explicit
- [ ] 73 code 28 explicit
- [ ] free-text code 31 explicit
- [ ] numeric field follows extra6-31 semantics
- [ ] SNR commands identified/formattable
- [ ] <....> placeholder decoded
- [ ] fixed upstream-derived vectors checked in
- [ ] transmission flags remain independent
- [ ] no command auto-policy introduced
- [ ] T057/T056 behavior unchanged
- [ ] T054/T055 DSP/WAV unchanged
- [ ] js8_engine pure/no-heap
- [ ] no FT8 changes
- [ ] Linux full CTest passes
- [ ] portable CTest passes
- [ ] external A_2_1 regression passes
- [ ] boundary/no-heap checks pass
- [ ] ASan/UBSan passes
- [ ] ADV build green
- [ ] git diff --check passes
- [ ] no unrelated cleanup

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

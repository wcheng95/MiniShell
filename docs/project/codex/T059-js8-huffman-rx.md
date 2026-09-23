# T059 — JS8 Normal Huffman DATA RX decoder

Status: READY

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

- [ ] exact 44-entry v3.0.3 Huffman table implemented
- [ ] Normal 10 DATA framing decoded
- [ ] upstream padding removed exactly for valid frames
- [ ] partial undecodable suffix stops like upstream huffDecode
- [ ] JSC/compressed frames return distinct unsupported/compressed status
- [ ] fixed upstream-derived vectors checked in
- [ ] all 44 table entries individually tested
- [ ] prefix-free property tested
- [ ] tail transmission flags remain independent
- [ ] js8_decode prints exact Huffman fragment for DATA frames
- [ ] synthetic WAV integrations pass
- [ ] real A_2_1 output remains unchanged
- [ ] T056-T058 semantics unchanged
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

    codex/T059-js8-huffman-rx

Codex:

1. read AGENTS.md, canonical JS8 docs, T058, and exact v3.0.3 Huffman/data functions;
2. implement a pure bounded Huffman DATA RX decoder only;
3. build independent source-pinned vectors;
4. test all table entries and framing/padding behavior;
5. extend js8_decode diagnostics for DATA only;
6. do not implement JSC or reassembly;
7. run the full existing JS8 gate set;
8. set Status to REVIEW;
9. fill notes/vector provenance;
10. push one reviewable commit and return SHA;
11. no PR and no Actions wait.

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

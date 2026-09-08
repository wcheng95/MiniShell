# MiniFT8-V3 RX — `message.h / message.c` Review

## Purpose

This is the final planned RX-0B source review. It examines the MiniFT8-V2 FT8 message codec from the RX side and defines the V3 boundary between:

```text
validated 77-bit payload
    -> protocol message classification/unpack
    -> typed protocol message
    -> rx_result_builder
    -> logical/application classification
```

The purpose is not to redesign the FT8 protocol. MiniFT8-V2 remains the behavioral reference while ownership and interfaces are cleaned.

The TX encoding half of `message.c` is noted where it affects shared ownership, but detailed encoder cleanup is deferred to the TX milestone.

## Overall assessment

The message codec contains useful protocol logic and should be **kept algorithmically**, but its output interface needs substantial cleanup.

Good existing properties:

- protocol type can be determined from the payload `i3/n3` bits for supported/recognized families;
- standard, non-standard, free-text, telemetry, DXpedition, and ARRL Field Day decoding already exist;
- standard-message field kinds are partly preserved (`CALL`, `GRID`, `RST`, tokens, etc.);
- callsign hashing is already abstracted through callbacks rather than hard-coded storage;
- the production V2 hashtable correctly supports 22-, 12-, and 10-bit lookup;
- the RX decode path has no platform/MiniShell dependency.

Main V3 cleanup areas:

- generic `field1/field2/field3 + offsets[3]` is not a sufficiently structured protocol result;
- callsign hash callbacks have no context pointer, so production storage is global/static even though its lookup behavior is correct;
- several FT8 message families are outside current MiniFT8-V2 support;
- output string APIs do not carry destination capacities;
- `ftx_message_t.hash` is really the decode CRC-derived quick key used together with full payload comparison, not a general collision-free message ID;
- encode and decode responsibilities share one large file, including TX-only parsing machinery.

## Current V2 protocol type model

`message.h` defines message-type names including:

```text
FREE_TEXT       0.0
DXPEDITION      0.1
EU_VHF          0.2
ARRL_FD         0.3 / 0.4
TELEMETRY       0.5
CONTESTING      0.6
STANDARD        i3 1 / 2
ARRL_RTTY       i3 3
NONSTD_CALL     i3 4
WWROF           i3 5
UNKNOWN
```

This is why V3 should preserve message type directly for the message families it supports rather than infer semantics again from rendered text.

### Supported behavior versus protocol namespace

MiniFT8-V2 does not support every FT8 message family, and that is acceptable scope.

For example:

1. `FTX_MESSAGE_TYPE_CONTESTING` exists in the enum, but `i3=0,n3=6` currently becomes `UNKNOWN` in the classifier.
2. `EU_VHF`, `ARRL_RTTY`, and `WWROF` can be identified by the type classifier, but the generic `ftx_message_decode()` switch has no structured decoder for them and returns `ERROR_TYPE`.

These facts do **not** require RX-1 to broaden protocol support. Structural cleanup preserves currently supported V2 behavior first.

For V3 API design, it is still useful to keep two concepts separate:

```text
payload/type family recognized by the protocol layer
        versus
structured decoder implemented by MiniFT8
```

A valid payload may therefore conceptually have:

```text
protocol_type = known/recognized type
parse_status  = supported / unsupported / malformed
```

But adding support for a previously unsupported message family is a separate feature step with its own vectors and tests, not part of ownership refactoring.

## Generic three-field output is not enough

V2 exposes:

```text
ftx_message_offsets_t
    types[3]
    offsets[3]
```

and generic decoded strings:

```text
field1
field2
field3
```

This is useful for display but not sufficient as the canonical V3 protocol representation.

Examples:

### Standard

```text
CQ W1XYZ FN42
```

Three fields happen to fit naturally.

### ARRL Field Day

Internally the decoder already knows:

```text
call_to
call_de
has_r
tx_num
class
section
```

but it formats the exchange back into one `extra` string and marks the third field `UNKNOWN`.

### DXpedition

Internally the decoder knows:

```text
RR73 callsign
report-target callsign
fox callsign
report dB
```

but the current generic interface joins multiple semantic values into `field3`.

Therefore V3 must not make the three display fields the source of truth.

## Recommended V3 protocol result

Exact C syntax is deferred to RX-1B, but the semantic shape should be a tagged result:

```text
Ft8ProtocolMessage
    raw payload identity
    protocol message type
    parse status
    canonical rendered text
    typed message-specific data
```

Conceptually the typed data is a union/tagged family such as:

```text
STANDARD
    call_to
    call_de
    extra_kind
    grid/report/token value

NONSTD_CALL
    call_to
    call_de
    report/token

ARRL_FD
    call_to
    call_de
    has_r
    transmitter_count
    class
    section

DXPEDITION
    rr73_call
    report_call
    fox_call
    report_db

FREE_TEXT
    text

TELEMETRY
    raw telemetry bytes / canonical hex
```

Unsupported message families need not gain a structured variant until MiniFT8 intentionally adds support.

Rendered text remains valuable for UI/logging/debugging, but it is derived convenience data:

```text
typed protocol data
    -> rendered canonical text
```

not the other way around.

## Free-text CQ exception remains above protocol decode

The user-approved exception remains explicit:

```text
protocol type = FREE_TEXT
canonical text matches exactly:
    CQ <nnn|AAAA> <valid callsign> [valid grid]
```

Then `rx_result_builder` may set:

```text
logical is_cq = true
```

while preserving:

```text
protocol type = FREE_TEXT
```

This is a narrow application classifier, not generic re-tokenization of ordinary typed FT8 messages.

## Callsign hash ownership

V2 uses:

```c
ftx_callsign_hash_interface_t
    lookup_hash(...)
    save_hash(...)
```

The abstraction itself is good. The production V2 hashtable correctly resolves 22-, 12-, and 10-bit hashed callsigns and saves newly decoded standard/non-standard calls for later resolution.

The interface itself, however, has no caller/context pointer. The real V2 implementation therefore uses global storage.

V3 direction:

```text
ft8_engine
    |
    `-- Ft8HashStore
          storage
          lookup/save
          age/lifecycle
```

with a context-aware internal interface conceptually like:

```text
context
lookup(context, hash_type, hash, out_call)
save(context, callsign, n22)
```

The exact table implementation and capacity remain private to the engine/hash-store module.

The hash store persists across RX slots because standard/non-standard calls decoded now can resolve shortened hashes later. Slot aging is explicit lifecycle behavior rather than a hidden global side effect.

This is an ownership/interface cleanup, not a correction of the production V2 hash algorithm.

The old `tests/tx_e2e/decode_helper.cpp` map is only a simplified host-test stub and should not be confused with the production hashtable.

### Hash callback validation

V2 helper functions check whether `hash_if` itself is NULL but assume non-NULL function pointers when the interface exists. V3 initialization should validate a supplied hash-store interface once so malformed partial interfaces cannot crash inside message decoding.

## Payload identity and duplicate suppression

`ftx_message_t` contains:

```text
payload[10]
hash
```

During candidate decode, `hash` is assigned from the calculated 14-bit CRC and V2 then combines that value with a full payload comparison for duplicate suppression.

V3 should make the semantics explicit:

```text
CRC14 / quick key      diagnostic or fast prefilter
full 77-bit payload    canonical exact identity for dedupe
```

A 14-bit CRC alone must never be treated as collision-free message identity.

For Cardputer-class workloads, exact comparison of the fixed 10-byte payload is cheap and simple.

## Output-buffer interface cleanup

Current public decode helpers accept raw `char*` outputs without output capacities. Correctness depends on every caller knowing the hidden maximum size expected by each function.

RX-1A found one concrete consequence: telemetry renders 18 hex characters plus a terminating NUL while the generic V2 path used a 16-byte temporary buffer. MiniFT8-V2 PR #44 fixed that isolated safety bug by increasing the temporary buffer to 19 bytes and adding a direct telemetry regression test.

V3 should still avoid this class of interface by preferring fixed-size fields owned by the typed output structure, or otherwise passing explicit capacities.

## Source-file ownership cleanup

`message.c` currently mixes:

```text
message type classification
RX payload unpacking
TX message parsing/packing
callsign codec helpers
callsign hash adapter helpers
Field Day helpers
DXpedition helpers
free-text/telemetry helpers
```

For V3, one giant protocol file is unnecessary. A reasonable eventual internal organization is conceptually:

```text
ft8_engine/protocol/
    message_types / shared definitions
    message_decode
    callsign_codec
    hash_store
    message_encode        later TX milestone
```

Exact filenames are not locked. RX-1B decides final boundaries top-down; the rule is ownership/readability, not file count.

TX-only parsing details such as the current DXpedition text parser are deferred to the TX review. RX cleanup should not be delayed by encoder refactoring.

## RX message-codec boundary

After this review, the RX-side engine boundary is:

```text
candidate decoder
    -> CRC-valid 77-bit payload
    -> message type classifier
    -> typed protocol unpacker
    -> Ft8ProtocolMessage
```

Then:

```text
Ft8ProtocolMessage
    -> rx_result_builder
         station-aware DXpedition logical transformation
         typed CQ classification
         free-text CQ exception
         addressed-to-me classification
         optional logical dedupe
    -> RxMessage / RxBatch
```

The message codec does **not** own:

```text
mycall application semantics
whether to reply
IgnoreList
AutoSeq state
TX decisions
UI sorting
ADIF logging
RTC correction
MiniShell
```

A future deep-search decode pass may use station identity before or during payload recovery, but once a payload is recovered the protocol message decoder remains the same typed codec.

## RX-0B source classification after message review

```text
supported V2 type classification       KEEP
standard/nonstandard unpack math       KEEP
Field Day unpack math                  KEEP; expose structured fields
DXpedition unpack math                 KEEP; expose structured fields
free-text/telemetry unpack             KEEP
callsign encode/decode helpers         KEEP shared protocol mechanics
production hash lookup algorithm       KEEP
hash ownership/interface               CLEAN substantially
3-field offsets as canonical model     DROP as domain representation
rendered text                           KEEP as derived convenience
unsupported message families           DEFER as separate features
TX message parsing/packing              DEFER to TX milestone
```

## Golden tests required before RX-1 changes behavior

At minimum, typed message-codec tests should cover the V2-supported RX behavior we depend on:

```text
standard CQ + grid
standard directed report
standard RR73 / 73
non-standard call
hashed-call lookup hit
hashed-call lookup miss
DXpedition type 0.1
ARRL Field Day 0.3
ARRL Field Day 0.4
free text
telemetry
```

Unsupported message families do not need to become supported merely to make the structural test suite exhaustive. If support is added later, each new family gets dedicated protocol vectors/tests.

The free-text CQ logical-classification tests belong in `rx_result_builder`, not in the protocol codec tests.

## RX-0B conclusion

No MiniShell or application-policy leakage was found in the RX protocol mathematics. The main cleanup is representation and state ownership, not protocol redesign.

The clean V3 sequence is now fully defined:

```text
streaming PCM
    -> explicit monitor/workspace
    -> waterfall
    -> candidate search
    -> likelihood / LDPC / CRC
    -> CRC-valid payload
    -> typed protocol message
    -> rx_result_builder
    -> RxBatch
```

This completes the planned RX-0B source reviews. RX-1A froze golden evidence; RX-1B now defines the top-down module/interface design before any source migration.

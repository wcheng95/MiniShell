# MiniFT8-V3 RX-1F — Typed Protocol Message Codec and Ft8ProtocolSlot

Status: **COMPLETE**

RX-1F migrates the supported receive-side FT8 message unpacking behind the MiniFT8 `ft8_engine` boundary and connects it to the explicit RX-1E `Ft8HashStore`.

RX-1F remains a structural-cleanup stage. It does not add station-aware classification, AutoSeq policy, new FT8 message families, DSP changes, SNR changes, TX encoding, or UI behavior.

## 1. Scope

The implemented receive path now reaches:

```text
validated 10-byte FT8 payload
        |
        v
protocol type classification
        |
        v
structured message unpacking
        |
        +--> Ft8HashStore lookup/save
        |
        v
Ft8ProtocolMessage
        |
        v
exact-payload dedupe
        |
        v
Ft8ProtocolSlot
```

`Ft8ProtocolMessage` is the protocol-level result. `rx_result_builder` remains the later owner of station-aware facts such as `is_cq`, `is_to_me`, and DXpedition relevance.

## 2. Representation rule

RX-1F makes the typed protocol representation authoritative.

```text
payload
  -> protocol type
  -> parse status
  -> typed message-specific fields
  -> canonical rendered text
```

Rendered text is derived convenience data. It is not reparsed to recover structure.

Protocol type and parse status are deliberately separate facts:

```text
Ft8ProtocolType
    FREE_TEXT
    DXPEDITION
    EU_VHF
    ARRL_FD
    TELEMETRY
    STANDARD
    ARRL_RTTY
    NONSTD_CALL
    WWROF
    UNKNOWN

Ft8ProtocolParseStatus
    OK
    UNSUPPORTED
    MALFORMED
```

This preserves the distinction between a recognized FT8 family and a family MiniFT8 currently knows how to unpack structurally.

## 3. Typed message data

Implemented in:

```text
apps/ft8/src/ft8_engine/ft8_message_codec.h
apps/ft8/src/ft8_engine/ft8_message_codec.c
```

`Ft8ProtocolMessage` contains:

```text
exact 10-byte payload
protocol type
parse status
unresolved-hash flag
canonical text
message-specific tagged data
candidate/LDPC/CRC diagnostics copied from RX-1D
```

Supported typed variants are:

```text
STANDARD
    call_to
    call_to field kind
    call_de
    extra
    extra field kind

NONSTD_CALL
    is_cq at protocol-message level
    call_to
    call_de
    terminal token: none / RRR / RR73 / 73

ARRL_FD
    call_to
    call_de
    has_r
    transmitter_count
    class_letter
    section

DXPEDITION
    rr73_call
    report_call
    fox_call
    report_db

FREE_TEXT
    exact decoded free text

TELEMETRY
    9 raw telemetry bytes
    18-character uppercase hexadecimal rendering
```

The `NONSTD_CALL.is_cq` field is a direct fact encoded by that protocol family. It is not the later application-level FREE_TEXT CQ classifier.

## 4. Supported behavior preserved

The pinned MiniFT8-V2 receive codec behavior is preserved for the message families used by MiniFT8:

```text
STANDARD
NONSTD_CALL
FREE_TEXT
DXPEDITION
ARRL_FD
TELEMETRY
```

RX-1F does not broaden V2 scope merely because the FT8 namespace contains additional families.

Currently recognized but structurally unsupported families remain explicit `UNSUPPORTED` results:

```text
EU_VHF
ARRL_RTTY
WWROF
```

V2 type `i3=0,n3=6` remains `UNKNOWN`; RX-1F does not silently add contesting support.

## 5. Callsign hash integration

RX-1F consumes the RX-1E `Ft8HashStore` directly. There is no callback interface without context and no process-global callsign table.

The codec preserves the three V2 hash widths used on receive:

```text
standard hashed callsign       22-bit lookup
non-standard message           12-bit lookup
DXpedition fox callsign         10-bit lookup
```

Directly decoded standard/non-standard callsigns are saved back into the supplied store using the preserved V2 callsign hash mathematics:

```text
11-character base-38 callsign value
        |
        v
((47055833459 * n58) >> 42) & 0x3FFFFF
        |
        v
22-bit canonical callsign hash
```

### Hash miss semantics

A missing shortened/full callsign hash is **not** a malformed FT8 message.

V2 behavior is preserved:

```text
lookup hit   -> <CALLSIGN>
lookup miss  -> <...>
```

RX-1F additionally records:

```text
has_unresolved_hash = true
```

so later application/UI logic can distinguish an unresolved fact without reparsing rendered text.

## 6. Exact payload remains identity

RX-1F keeps the RX-1A/RX-1D rule:

> Exact 10-byte payload bytes are the protocol-message identity.

CRC values remain diagnostics/validation metadata and are not treated as collision-free message IDs.

`Ft8ProtocolSlot` therefore deduplicates using exact payload comparison.

## 7. Ft8ProtocolSlot memory ownership

`Ft8ProtocolSlot` owns no allocator and no hidden message array.

The caller supplies message storage:

```text
caller-owned Ft8ProtocolMessage[]
        |
        v
Ft8ProtocolSlot
    slot_id
    status
    storage pointer
    capacity
    message_count
```

This makes capacity and RAM cost explicit before the complete `ft8_engine` is assembled.

Slot behavior is explicit:

```text
EMPTY
OK
FULL
```

and add results distinguish:

```text
ADDED
DUPLICATE
ERR_FULL
ERR_INVALID
```

A valid but currently unsupported protocol payload may still be retained in a slot with `parse_status=UNSUPPORTED`; protocol recovery and structured MiniFT8 support are separate facts.

## 8. Frozen RX-1A codec vectors

RX-1F reproduces all five frozen RX-1A receive-codec vectors exactly.

### Standard CQ

```text
payload   000000206016500A1988
type      STANDARD
text      CQ W1XYZ FN42
```

Typed fields include:

```text
call_to      CQ
call_de      W1XYZ
extra        FN42
extra_kind   GRID
```

### ARRL Field Day

```text
payload   0C1690C536B4268176C0
type      ARRL_FD
text      W6ABC AG6AQ R 1B SCV
```

Typed fields:

```text
call_to            W6ABC
call_de            AG6AQ
has_r              true
transmitter_count  1
class_letter       B
section            SCV
```

### DXpedition

```text
payload   326C137BC6A185277040
type      DXPEDITION
text      K1ABC RR73; W9XYZ <KH1/KH7Z> -08
```

Typed fields preserve the two callers, fox callsign, and report independently.

### Non-standard CQ

```text
payload   00003E4A34A86EEB8460
type      NONSTD_CALL
text      CQ PJ4/KA1ABC
```

### Free text

```text
payload   2C91495D9F3A73119400
type      FREE_TEXT
text      CQ POTA W1XYZ
```

It remains protocol type `FREE_TEXT`. The later narrow FREE_TEXT-CQ application classifier remains in `rx_result_builder`.

## 9. Telemetry safety

RX-1F uses an explicit 19-byte storage field for the 18 hexadecimal characters plus terminating NUL.

Regression vector:

```text
raw bytes  01 23 45 67 89 AB CD EF 01
text       0123456789ABCDEF01
```

This preserves the earlier V2 telemetry safety correction while removing the old implicit caller-buffer-size contract.

## 10. Unit tests

Implemented in:

```text
tests/ft8_message_codec_rx1f_test.c
```

Coverage includes:

```text
all five frozen RX-1A codec vectors
STANDARD typed fields and hash-store save
ARRL Field Day typed fields
DXpedition 10-bit hash hit
DXpedition 10-bit hash miss
NONSTD_CALL CQ
FREE_TEXT
TELEMETRY 18-character output
synthetic STANDARD 22-bit hash hit/miss
synthetic NONSTD_CALL 12-bit hash + RR73
recognized unsupported family
unknown type 0.6
malformed Field Day payload
RX-1D diagnostic propagation
Ft8ProtocolSlot exact-payload dedupe
Ft8ProtocolSlot full-capacity behavior
```

RX-1E separately proves hash-store multi-instance independence, aging, trim-hole probing, and V2 capacity/eviction behavior.

## 11. CI result

On the RX-1F code-bearing head:

```text
Linux full suite               16/16 PASS
ft8_message_codec_rx1f_unit    PASS
RX-1C pinned waterfall golden  PASS
RX-1D pinned payload golden    PASS
ADV registry unit              PASS
```

The ADV firmware build was still compiling at the time RX-1F was documented. RX-1F is not yet linked into the ADV production MiniFT8 composition, so no RX-1F hardware validation is required at this stage.

## 12. RX-1F exit criteria

```text
[done] MiniFT8-owned typed protocol representation
[done] protocol type separate from parse status
[done] V2-supported RX message families preserved
[done] recognized unsupported families remain explicit
[done] exact five RX-1A codec vectors preserved
[done] 22/12/10-bit Ft8HashStore integration
[done] hash misses remain valid messages with explicit unresolved state
[done] direct calls saved into explicit hash store
[done] canonical text derived from typed data
[done] telemetry buffer contract made explicit/safe
[done] exact payload dedupe in Ft8ProtocolSlot
[done] caller-supplied slot message storage
[done] no MiniShell/platform/UI/AutoSeq/TX dependency
[done] no station-aware rx_result_builder policy mixed into codec
```

## 13. Next stage

Next active stage:

**RX-1G — assemble and regression-test the pure cleaned `ft8_engine` boundary end-to-end.**

RX-1G should connect the already-clean RX-1C monitor, RX-1D candidate decoder, RX-1E hash store, and RX-1F message codec under one engine lifecycle and reproduce the frozen 6 kHz golden result without adding frontend, MiniShell Audio, station-aware classification, or algorithm changes.

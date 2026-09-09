# RX-5 — Pure MiniFT8 RX Assembly

Status: **COMPLETE**

RX-5 closes the pure MiniFT8 receive-domain assembly before MiniShell Audio is connected.

## Goal

Assemble the already-clean RX components without adding a second application coordinator:

```text
12 kHz / S16 / 2-channel
        |
        v
RxFrontend
        |
        | 6 kHz mono float
        v
RxSlotFramer
        |
        | BEGIN_WINDOW / 960-sample ENGINE_BLOCK /
        | FINALIZE_WINDOW / STREAM_RESET
        v
Ft8Engine
        |
        v
Ft8ProtocolSlot
        |
        v
RxResultBuilder
        |
        v
RxBatch
```

RX-5 is intentionally MiniShell-independent and Linux-host tested. No Audio API, UI, AutoSeq, TX, Control, storage, or ADV-backend dependency is introduced.

## Coordinator rule

RX-5 does **not** add an `rx_pipeline`, `rx_manager`, or other production coordinator.

`app_controller` remains MiniFT8's sole application coordinator. The RX-5 end-to-end wiring lives in a host reference harness for now. When production RX is integrated, `app_controller` will compose the same passive/stateful owners.

This preserves the dependency direction fixed in RX-1B:

```text
app_controller
    |
    +-- rx_audio_adapter        later, RX-6
    +-- rx_frontend
    +-- rx_slot_framer
    +-- ft8_engine
    `-- rx_result_builder
```

## `rx_result_builder` ownership

RX-5 adds the final pure application-domain module:

```text
apps/ft8/src/rx_result_builder/
    rx_result_builder.h
    rx_result_builder.c
    README.md
```

It converts typed protocol facts into caller-owned application facts:

```text
Ft8ProtocolSlot
    -> preserve exact 10-byte payload identity
    -> preserve protocol type / parse status
    -> preserve decoder diagnostics
    -> expose type-dependent call fields
    -> classify factual is_cq
    -> classify factual is_to_me
    -> RxBatch
```

The builder owns no allocator. `RxMessage[]` storage is supplied by the caller.

It also has no process-global parser state. An initial use of `strtok()` was removed during RX-5 because hidden tokenizer state would violate the explicit-ownership rule; FREE_TEXT classification now uses a small stack-local tokenizer.

## `RxMessage` / `RxBatch`

`RxMessage` carries application-facing facts while retaining the authoritative protocol identity:

```text
payload[10]
protocol_type
parse_status
has_unresolved_hash
is_cq
is_to_me
canonical_text
call_to
call_de
extra
candidate diagnostics
LDPC / CRC diagnostics
```

`RxBatch` contains:

```text
slot_id
protocol slot status
caller-owned RxMessage[]
message_count
capacity
```

No reply decision is encoded in either type.

## Local-station context

`RxResultBuilderConfig` may contain the local callsign. RX-5 uses it only for factual classification such as:

```text
message addressed to AG6AQ
    -> is_to_me = true
```

Resolved hash text such as `<AG6AQ>` is compared to the same local identity.

This remains context, not QSO policy. `rx_result_builder` does not decide:

```text
should_reply
selected_station
next_tx_stage
TX armed
IgnoreList action
UI priority/order
logging action
```

Those remain responsibilities above this boundary.

## CQ classification

Typed protocol structure is used whenever available.

For example, STANDARD and NONSTD_CALL messages use their structured destination/token fields rather than reparsing canonical text.

The previously locked FREE_TEXT exception is implemented here:

> A FREE_TEXT message may additionally be classified as logical CQ only when it matches `CQ <nnn|AAAA> <valid-callsign> [grid]`. Its protocol type remains FREE_TEXT.

Examples:

```text
CQ POTA K7XYZ     -> is_cq = true,  protocol_type = FREE_TEXT
CQ 123 K7XYZ DM43 -> is_cq = true,  protocol_type = FREE_TEXT
CQ HELLO WORLD    -> is_cq = false, protocol_type = FREE_TEXT
```

This classification is factual only; `is_cq=true` does not mean MiniFT8 should reply.

## Unit coverage

`tests/rx_result_builder_rx5_test.c` covers:

```text
local callsign normalization
STANDARD CQ classification
resolved <AG6AQ> target -> is_to_me
FREE_TEXT POTA CQ exception
false CQ-shaped FREE_TEXT rejection
caller output-capacity failure
post-destroy lifecycle failure
```

Reference result:

```text
rx_result_builder_rx5_test: PASS
```

## Pure RX golden

`tests/rx5_pure_assembly_reference.c` exercises the whole pure RX domain.

The pinned RX-1A 6 kHz CQ reference is expanded into the locked MiniFT8 transport representation:

```text
6 kHz mono S16 reference
    -> duplicate each sample into two 12 kHz frames
    -> duplicate into both ordered channels
    -> 12 kHz / S16 / stereo transport
```

The remaining part of the 15-second slot is zero-filled. Transport is intentionally delivered in **257-frame chunks**, so frontend decimation pairs repeatedly cross transport boundaries.

The full path is therefore:

```text
12 kHz S16 stereo
    -> RxFrontend
    -> 6 kHz mono float
    -> RxSlotFramer
    -> 93 complete 960-sample blocks
    -> Ft8Engine
    -> Ft8ProtocolSlot
    -> RxResultBuilder
    -> RxBatch
```

Frozen result:

```text
RX5 slot=12345 blocks=93 messages=1 cq=1 to_me=0 text="CQ W1XYZ FN42"
rx5_pure_assembly_reference: PASS
```

The exact payload remains:

```text
000000206016500A1988
```

This proves that the complete pure receive-domain assembly preserves the RX-1A through RX-4 behavior while adding only factual application projection.

## Fault/ownership properties

RX-5 preserves the existing containment rules:

```text
RxFrontend owns only format/rate adaptation state
RxSlotFramer owns only slot/sample/block framing state
Ft8Engine owns only FT8 DSP/protocol state
RxResultBuilder owns only factual projection state
app_controller remains the sole future coordinator
```

No module reaches through another module to own its underlying resource.

## Explicitly deferred

RX-5 does not add or change:

```text
MiniShell Audio lifecycle
rx_audio_adapter
WAV provider integration
real QMX audio
ADV audio backend
UI display of decoded messages
AutoSeq / reply policy
TX / Control
SNR algorithm
FFT / OSR / candidate policy
LDPC / CRC behavior
front-end filtering
```

## Exit criteria

RX-5 is complete because:

```text
[PASS] rx_result_builder is a small explicit owner
[PASS] no second RX coordinator exists
[PASS] caller owns RxBatch message storage
[PASS] factual CQ/to-me classification is tested
[PASS] no hidden tokenizer/global parser state remains
[PASS] 12k stereo -> RxBatch golden passes
[PASS] exact payload and canonical CQ text remain unchanged
[PASS] no MiniShell or ADV dependency entered the pure RX domain
```

## Next

RX-6 connects the already-working pure RX chain to the MiniShell Audio API on the Linux backend:

```text
MiniShell WAV Audio
12 kHz / S16 / 2-channel
        |
        v
rx_audio_adapter
        |
        v
RxFrontend
        |
        v
RxSlotFramer
        |
        v
Ft8Engine
        |
        v
RxResultBuilder
        |
        v
RxBatch
```

The critical RX-6 architectural test is provider substitution: replacing the Linux WAV provider later with QMX or another provider must not require changes inside `rx_frontend`, `rx_slot_framer`, `ft8_engine`, or `rx_result_builder`.

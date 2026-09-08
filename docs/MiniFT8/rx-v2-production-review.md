# MiniFT8-V3 RX — V2 Production Decode Review

## Purpose

This document continues RX-0B by reviewing MiniFT8-V2 `decode_monitor_results()` as the production behavioral reference.

The purpose is not to copy the function. The purpose is to separate the responsibilities currently combined in it and assign each responsibility to a clean V3 owner before RX implementation begins.

## V2 production flow

At a high level, the current V2 function does all of this:

```text
finalized waterfall
    -> candidate search
    -> slot/hash maintenance
    -> noise estimate
    -> candidate decode
    -> payload dedupe
    -> message decode
    -> frequency/time/SNR-like measurement
    -> DXpedition station-specific rewrite
    -> rendered-text dedupe
    -> field parsing
    -> CQ/to-me classification
    -> RX/TX trace logging
    -> RTC correction
    -> UI sorting
    -> IgnoreList filtering
    -> AutoSeq
    -> TX/beacon arming
    -> UI handoff
    -> cross-core decode-applied bookkeeping
```

This is good behavioral evidence but is not a suitable V3 module boundary.

## Production policy observed

The current production decoder uses:

```text
candidate capacity     50
minimum sync score      5
max LDPC iterations    25
slot result             multiple unique messages
```

Duplicate candidates that decode to the same protocol payload are removed using message hash plus payload comparison.

These values are the V2 behavioral baseline during structural cleanup. They are policy/configuration, not protocol constants.

## Responsibility map

| V2 responsibility | V3 owner | Decision |
| --- | --- | --- |
| candidate search | `ft8_engine` / candidate finder | keep algorithm first |
| candidate capacity / threshold | explicit FT8 decode policy | no hidden constants |
| candidate LDPC decode | `ft8_engine` / candidate decoder | keep algorithm first |
| payload/hash duplicate suppression | `ft8_engine` slot decode | intrinsic decoder cleanup |
| payload -> protocol message | `ft8_engine` message codec | keep structured result |
| frequency offset calculation | `ft8_engine` result metadata | intrinsic measurement |
| candidate time offset | `ft8_engine` result metadata | intrinsic measurement |
| candidate sync score | `ft8_engine` diagnostic metadata | never call SNR |
| callsign-hash lookup/save | explicit MiniFT8 FT8 hash-store state | domain state, not MiniShell |
| callsign-hash aging | hash-store lifecycle, triggered once per RX slot | remove global hidden policy |
| slot identity/parity | `rx_slot_framer` / `app_controller` | decoder must not read RTC |
| noise/SNR estimation | FT8 measurement code, initially V2-compatible | validate separately |
| DXpedition rewrite for `mycall` | RX result/application normalization | station-specific; not protocol engine |
| rendered-text duplicate suppression | RX result builder if still needed | distinct from payload dedupe |
| `field1/field2/field3` text reparsing | drop | preserve message codec structure instead |
| CQ classification | RX result/application classification | not decoder math |
| addressed-to-me classification | RX result/application classification | requires station identity |
| IgnoreList | AutoSeq/application policy | never decoder |
| RX/TX trace logging | application logging edge | never decoder |
| median decode-time RTC correction | application timing policy + MiniShell Time | engine only supplies time offsets |
| presentation sorting (`to_me`, CQ, others) | UI/application presentation | never decoder |
| AutoSeq call | `app_controller -> AutoSeq` | no direct RX->AutoSeq coupling |
| pending TX / beacon arming | `app_controller -> TX` | never decoder |
| UI handoff | `app_controller -> UiModel` | never decoder |
| `g_decode_in_progress` / applied-slot cross-core guard | V3 pipeline/lifecycle coordination | do not import V2 globals |
| heap/stack instrumentation | tests/debug instrumentation | not decoder contract |

## Engine boundary after this review

The core FT8 engine should stop at a station-independent decoded slot result:

```text
engine-native sample blocks
    -> monitor / waterfall
    -> candidate search
    -> candidate decode
    -> payload dedupe
    -> message codec
    -> FT8 decoded-message records
```

Conceptually:

```text
Ft8DecodedSlot
    protocol
    messages[]

Ft8DecodedMessage
    payload identity
    message type
    structured protocol fields
    rendered canonical text
    candidate score
    frequency offset
    time offset
    measured SNR if available and valid
    optional LDPC/CRC diagnostics
```

The engine must not know:

```text
my callsign
IgnoreList
CQ display priority
AutoSeq
TX state
beacon state
UI pages
ADIF/RxTx files
RTC implementation
MiniShell
```

## Preserve structure; do not reparse rendered text

V2 stores RX lines as UI-shaped data containing:

```text
text
field1
field2
field3
snr
offset_hz
slot_id
time_s
is_cq
is_to_me
```

The production path currently calls `ftx_message_decode()` and receives `ftx_message_offsets_t`, but later derives `field1/field2/field3` again by tokenizing the rendered text.

V3 should avoid this round trip:

```text
protocol payload
    -> structured message codec result
    -> application classification / UI formatting
```

not:

```text
protocol payload
    -> text
    -> parse text again
    -> recover fields
```

This is both cleaner and safer for special message types such as Field Day, DXpedition, non-standard calls, and future protocol additions.

## Two levels of duplicate suppression

V2 currently performs two kinds of dedupe. They should remain conceptually separate.

### 1. Protocol-payload dedupe

Several nearby candidate locations may decode to the same FT8 payload.

```text
candidate A --+
candidate B --+-> same payload -> one decoded protocol message
candidate C --+
```

This belongs inside `ft8_engine`.

### 2. Logical/rendered-message dedupe

Station-specific transformation can make distinct/raw protocol representations appear as the same logical application message.

If V3 still needs this, it belongs in the RX result/application normalization layer, after protocol decode.

Do not combine the two dedupe mechanisms.

## DXpedition handling

V2 rewrites a decoded DXpedition type 0.1 message based on the local callsign so AutoSeq sees a normal logical message addressed to this station.

That behavior is valuable, but the local callsign dependency means it must not live in the station-independent FT8 decoder core.

Preferred V3 direction:

```text
ft8_engine
    -> preserve the complete structured DXpedition message

RX result/application normalization
    + station identity
    -> derive the logical message(s) relevant to this station
```

This keeps protocol decoding reusable and makes the transformation directly unit-testable.

## SNR finding

Production V2 does have a separate SNR-like calculation; unlike `decode_helper.cpp`, it does not simply label candidate score as SNR.

Current V2 behavior is approximately:

```text
noise floor = 25th percentile of waterfall magnitudes
candidate level = one waterfall location near candidate start
reported value = candidate level - noise floor
clamped to [-30, 99]
```

This should be preserved initially only as a **V2-compatible measurement heuristic**. It is not automatically a calibrated FT8 SNR definition merely because the UI labels it SNR.

V3 rules:

- `candidate_score` and `snr_db` are different fields;
- if SNR is unavailable, represent it as unavailable rather than substituting candidate score;
- structural cleanup must not silently change the V2 displayed estimate;
- improving/calibrating SNR is a later measured algorithm change with its own tests.

## Decode timing and RTC correction

Production V2 takes the median `time_s` of more than three decoded messages and may adjust the software RTC by a bounded amount.

The useful separation for V3 is:

```text
ft8_engine
    -> per-message decode time offset

RX/app timing policy
    -> aggregate offsets / decide correction

MiniShell Time ABI
    -> own/apply time update
```

The decoder never adjusts a clock.

## Sorting and AutoSeq

Production V2 sorts decoded entries:

```text
to-me first
CQ second
others last
```

and sorts CQ entries by displayed SNR. It then constructs a `to_me` collection, applies IgnoreList filtering, calls AutoSeq, and may arm TX or beacon CQ.

V3 keeps those as separate application steps:

```text
Ft8DecodedSlot
    -> RX result/application normalization
    -> RxBatch
    -> app_controller
           +-> UiModel/presentation sorting
           +-> AutoSeq
                    -> TxRequest
                    -> app_controller
                    -> TX
```

RX never directly calls AutoSeq or TX.

## Slot-completion bookkeeping

V2 has `g_decode_in_progress` and `g_decode_applied_slot_idx` because decode and TX triggering run across cores/tasks and must maintain the invariant that TX for the next slot does not begin before the previous slot's decode has been applied.

That invariant is important; the V2 global implementation is not.

V3 should model the lifecycle explicitly:

```text
slot N finalized
    -> decode slot N
    -> RxBatch N delivered/applied
    -> slot N complete
    -> later TX decision may proceed
```

The exact concurrency mechanism can differ by backend/platform without leaking into `ft8_engine`.

## RX result builder boundary

After this review, the application-side boundary is clearer:

```text
ft8_engine output
    |
    v
rx_result_builder
    - station-aware logical normalization
    - DXpedition relevance transformation
    - CQ / addressed-to-me classification
    - optional logical-message dedupe
    |
    v
RxBatch
    |
    v
app_controller
```

`rx_result_builder` must not contain FFT, Costas search, LDPC, CRC, or MiniShell calls.

## What becomes golden during structural cleanup

Hard behavioral invariants:

```text
same valid protocol payloads/messages from same test audio
same special-message decoding semantics
same callsign-hash resolution behavior
no duplicate protocol payloads in slot output
```

Diagnostic/reference values, recorded but not initially exact-hard-golden:

```text
candidate count
candidate score
candidate order
frequency/time estimates
V2 SNR heuristic
```

Algorithm changes are deferred until structure is clean and the golden suite is stable.

## RX-0B status

Completed reviews:

```text
decode_helper.cpp        complete
production decode_monitor_results() responsibility review   complete
```

Next source review before RX-1:

```text
monitor.h / monitor.c
```

That review will decide how to remove hidden singleton buffers, make memory ownership explicit, preserve streaming FFT/waterfall behavior, and expose initialization/workspace requirements without changing the FFT/decode mathematics yet.

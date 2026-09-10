# AS-2 — Pure Compact AutoSeq Core

AS-2 replaces the temporary `qso_scheduler` settings holder with the real MiniFT8-V3 AutoSeq owner.

Reference oracle:

```text
wcheng95/Mini-FT8
491e757ae6b1e4cfd2b9a6ba10f48b35643849e0
```

AS-2 is intentionally structural. It establishes the QSO state/queue owner and its pure event API, but does **not** yet connect decoded `RxMessage` objects to AutoSeq, render the real T UIScreen, create `TxIntent`, log a QSO, or touch radio/TX resources.

## Ownership

```text
ConfigService
    owns persisted station / AutoSeq settings
        |
        v
app_controller
    owns one AutoSeq instance
    orders future external events
        |
        v
auto_seq
    owns QSO contexts
    owns active/inactive queue
    owns protocol progression state
    owns retry counters / priority
```

`auto_seq` is pure application logic. It has no dependency on MiniShell, UI, filesystem/logging, Audio, Control, platform code, `Ft8Engine`, or `Ft8HashStore`.

The old `qso_scheduler` module is deleted in AS-2. There is no compatibility shim and therefore no second QSO/scheduling-policy owner.

## Fixed storage

```text
AUTO_SEQ_MAX_QUEUE = 30
```

One fixed array is split dynamically into two zones:

```text
index 0                                               index 29
+----------------------+------------+----------------------+
| active               | free       | inactive             |
| grows ->             |            |              <- grows|
+----------------------+------------+----------------------+
        active_count --+            +-- inactive_start
```

The implementation uses no heap and no dynamic strings.

Measured Linux x86-64 layout for the AS-2 definition:

```text
sizeof(QsoContext) =   56 bytes
sizeof(AutoSeq)    = 1712 bytes
```

Compile-time guards apply on every target:

```text
sizeof(QsoContext) <= 64 bytes
sizeof(AutoSeq)    <= 2048 bytes
```

The second guard includes all 30 contexts plus station/config and queue indices.

## QsoContext

A context owns QSO-lifetime facts only:

```text
dxcall
dxgrid
fd_rx_exchange
snr_tx
snr_rx
offset_hz
tx_parity
state
last_rx_kind
retry_counter
retry_limit
inactive_since_ms
compact flags
```

Flags currently reserve V2 facts for:

```text
logged
cabrillo_logged
field_day
park_after_signoff
freetext
```

AS-2 preserves these facts structurally even though logging, Field Day message generation, and FreeText scheduling are integrated in later stages.

## State is authoritative

V3 does **not** store V2's mutable `next_tx` field.

Normal QSO TX meaning is derived from state:

```text
REPLYING       -> TX1
REPORT         -> TX2
ROGER_REPORT   -> TX3
ROGERS         -> TX4
SIGNOFF        -> TX5
CALLING/IDLE   -> none in the AS-2 normal-QSO derivation
```

This removes a duplicated state variable and eliminates the V2 failure mode where a parked context could be reactivated with a stale `TX_NONE` value.

CQ/TX6 and one-shot FreeText are deliberately deferred to their later integration stages rather than forcing them into this normal-QSO mapping.

## Input event boundary

AS-2 defines a normalized `AutoSeqRxEvent` rather than accepting `RxMessage *`.

The event carries only facts AutoSeq owns or consumes:

```text
RX slot id
measured RX SNR
received report value
RX audio offset Hz
normalized message kind TX1..TX5
CQ / addressed-to-me / Field Day flags
DX callsign
grid
Field Day exchange
```

`app_controller` will map the selected/current `RxMessage` into this event in AS-3/AS-4. AutoSeq never retains an RX-batch pointer and never learns decoder/waterfall representation.

Time is also explicit: operations that park a context receive caller-supplied monotonic milliseconds. AutoSeq does not poll a clock.

## Queue behavior preserved from V2

AS-2 preserves these structural rules:

1. Active entries are scheduled from the front of the active zone.
2. Higher protocol state has higher priority.
3. For equal state, lower retry count has priority.
4. FreeText has a reserved highest-non-IDLE priority flag for later use.
5. Retry exhaustion before reports are exchanged may eliminate the context.
6. Retry exhaustion at REPORT or beyond moves the context inactive so QSO metadata survives.
7. An addressed message from an inactive DX reactivates that context with its metadata preserved.
8. A previously unknown TX3/R+report, TX4/RR73, or TX5/73 is ignored to prevent a metadata-losing reincarnated QSO.
9. When capacity is exhausted, the oldest inactive context is the eviction candidate.
10. Same-parity rotation is explicit and bounded.

One non-obvious V2 edge is preserved deliberately: if the queue contains 29 inactive entries plus one active entry and that final active entry is parked, V2 evicts the oldest inactive entry before inserting the parked context. The resulting all-inactive population is therefore 29, not 30. The AS-2 test records this behavior so a later cleanup cannot accidentally become an unmeasured behavior change.

## Pure API surface

AS-2 exposes operations in four groups:

```text
configuration
    init / clear
    set station
    Skip-TX1
    max retry

input / progression
    manual RX event
    addressed RX event
    TX-completion tick

queue controls
    drop active index
    rotate same parity

read-only projection
    active/inactive counts
    context copies
    caller-owned QsoView snapshots
```

No returned view owns AutoSeq storage. Snapshot/context getters copy data into caller-owned objects.

## AS-2 unit proof

`ft8_auto_seq_as2_unit` covers:

```text
station/config normalization
state -> TX derivation
manual selected-QSO start
Skip-TX1
addressed-message progression
SNR/report metadata preservation
unknown mid-QSO reincarnation guards
retry counting
active -> inactive parking
inactive -> active reactivation
priority ordering
same-parity rotation
drop semantics
retry-limit updates
30-entry bound
oldest-inactive eviction
V2 29-inactive full-boundary edge
read-only QsoView projection
```

The unit is compiled separately with `-Werror` and is also part of normal Linux CTest. The same `auto_seq.c` source is compiled into the ADV MiniFT8 application.

## Deferred to AS-3+

AS-2 deliberately does not implement:

```text
RxMessage -> AutoSeqRxEvent mapping
selected-CQ action -> QSO creation
real T UIScreen rows
automatic addressed-to-me batch processing
CQ/beacon lifecycle
FreeText lifecycle
Field Day message formatting
TxIntent generation
slot-boundary TX realization
TX audio/control
ADIF/Cabrillo side effects
```

Those are integration/policy stages layered on this core rather than reasons for AutoSeq to acquire dependencies.

## Exit condition

AS-2 is complete when:

```text
pure AutoSeq unit suite passes
normal Linux/FT8 reference tests remain green
ADV cross-build passes
qso_scheduler is absent
queue/state ownership is documented
```

After that, AS-3 may connect the already-defined AS-1 RX selection boundary to this core and project `AutoSeqQsoView` into the real multi-QSO T UIScreen.

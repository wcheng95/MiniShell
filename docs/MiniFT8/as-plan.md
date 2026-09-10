# MiniFT8-V3 AutoSeq Plan

Status: **COMPLETE — AS-0 through AS-8 complete; structural AutoSeq port closed**

AutoSeq was ported structurally first: preserve the proven MiniFT8-V2 AutoSeq behavior while replacing old ownership, dynamic data structures, and cross-module coupling with explicit V3 boundaries. AS-8 closes that structural port and records the deliberate V3 differences.

## 1. Behavioral reference

The AutoSeq oracle is pinned to MiniFT8-V2:

```text
repository  wcheng95/Mini-FT8
commit      491e757ae6b1e4cfd2b9a6ba10f48b35643849e0
files       main/autoseq.cpp
            main/autoseq.h
            docs/AUTOSEQ_ARCHITECTURE.md
            docs/AUTOSEQ_INACTIVE_QUEUE.md
            host_mock/*
```

During the structural port, V2 behavior was authoritative unless a V3 boundary/ownership rule or an explicitly approved V3 behavior required a different interface or result. Behavioral improvements after AS-8 must be recorded separately and measured one at a time.

One intentional representation simplification was approved and implemented in AS-2:

> `next_tx` is derived from QSO state rather than stored as independent mutable state.

Canonical mapping:

```text
REPLYING      -> TX1
REPORT        -> TX2
ROGER_REPORT  -> TX3
ROGERS        -> TX4
SIGNOFF       -> TX5
```

The next **state** still depends on the received message; only the redundant stored `next_tx` field is removed.

One intentional behavioral difference is also locked:

> When replying to CQ FD, V3 skips TX1 and immediately sends the local Field Day exchange as TX2.

The pinned V2 manual-touch path instead followed the global Skip-TX1 setting even for CQ FD. See `as-8-equivalence.md`.

## 2. Goals

The AS block:

- preserves current V2 QSO progression and scheduling behavior except explicitly documented V3 differences;
- preserves active/inactive queue semantics and late-reply reactivation;
- preserves retry behavior, priority sorting, same-parity rotation, drop behavior, Skip-TX1, CQ/FreeText one-shots, logging eligibility timing, and Field Day semantics except the deliberate CQ-FD direct-TX2 rule;
- moves AutoSeq into one explicit `auto_seq` module with one owner;
- uses fixed-size C data and no AutoSeq heap allocation;
- consumes factual `RxMessage`/`RxBatch` data rather than UI strings;
- exposes typed QSO views and TX intents rather than platform/radio operations;
- keeps `app_controller` as the sole application coordinator;
- makes the T UIScreen a visible testbench for multiple queued QSOs;
- verifies the same pure AutoSeq state owner in Linux and ADV gates.

## 3. Non-goals during the port

AS-1 through AS-8 did not redesign:

- ordinary QSO state progression;
- retry counts or timeout policy;
- queue priority/fairness policy;
- inactive-QSO policy;
- CQ one-shot semantics;
- FreeText priority;
- logging eligibility rules;
- TX waveform generation;
- CAT/control behavior;
- radio selection;
- decoder algorithms.

The CQ-FD direct-TX2 rule is the explicit exception because it was already a locked V3 requirement before AS-8.

## 4. Ownership

```text
Ft8Engine
    owns DSP, protocol decode, and Ft8HashStore
        |
        v
RxResultBuilder
    owns factual application projection
        |
        v
RxBatch / RxMessage
        |
        v
app_controller
    owns event ordering and cross-module coordination
        |
        +----------------------+
        |                      |
        v                      v
     auto_seq                ui_shell
  owns QSO policy          owns navigation
  and QSO queue            and AppAction
        |
        v
  QsoView / TxIntent
        |
        v
app_controller
        |
        +--> UiModel -> T UIScreen
        |
        `--> TX lifecycle / future logging owner
```

Hard rules:

- `auto_seq` does not call MiniShell APIs;
- `auto_seq` does not read `station.txt`;
- `auto_seq` does not call `ft8_engine` or `Ft8HashStore`;
- `auto_seq` does not render UI;
- `auto_seq` does not perform ADIF/Cabrillo/file I/O;
- `auto_seq` does not start radio/audio TX;
- `auto_seq` performs no heap allocation;
- `ui_shell` never owns or receives `QsoContext` pointers;
- `app_controller` is the only production coordinator between RX, AutoSeq, UI, TX lifecycle, and logging.

## 5. AutoSeq state storage

The V2 queue capacity is retained:

```text
AUTO_SEQ_MAX_QUEUE = 30
```

AS-2 preserves V2's single-array active/inactive layout:

```text
index 0                                              index 29
+----------------------+-------------+----------------------+
| active zone          | free space  | inactive zone        |
| grows ->             |             |              <- grows|
+----------------------+-------------+----------------------+
       active_count                  inactive_start
```

The `QsoContext` is fixed-size and contains QSO-lifetime facts only:

```text
dxcall / dxgrid
Field Day receive exchange
SNR sent / received
audio offset Hz
TX parity
state / last RX message kind
retry counter / limit
inactive timestamp
compact flags
```

Measured Linux x86-64 layout:

```text
sizeof(QsoContext) =   56 bytes
sizeof(AutoSeq)    = 1712 bytes
```

Compile-time guards enforce:

```text
sizeof(QsoContext) <= 64 bytes
sizeof(AutoSeq)    <= 2048 bytes
```

AS-8 also makes the no-heap rule mechanical: the MiniFT8 platform-boundary check rejects allocator calls inside `apps/ft8/src/auto_seq/`.

Use a plain integer flag word/mask rather than C implementation-defined bit-fields. Current flags reserve V2 facts for logged/Cabrillo/FD/park-after-signoff/FreeText state.

Do not store canonical display text as authoritative QSO state. Do not store `next_tx`; derive it from `state`.

## 6. Input contracts

AutoSeq consumes factual MiniFT8 data only.

For a selected or automatically processed RX message it needs, as applicable:

```text
slot_id
protocol type
is_cq
is_to_me
qso_kind
call_to
call_de
extra/grid/report fields
frequency/offset
RX SNR
resolved/unresolved hash status
```

AS-2 represents the subset needed by the pure state owner as a normalized `AutoSeqRxEvent`. AS-3 maps selected resolved CQs from real `RxMessage` data into that event. AS-4 adds factual ordinary-QSO `qso_kind/report_db` metadata and maps completed `is_to_me` messages automatically in decode order. AS-6 adds factual Field Day classification/exchange data and explicit CQ/FreeText configuration.

AutoSeq copies only QSO-lifetime facts into its own context. It must never retain a pointer into `RxBatch`, because the RX batch belongs to the RX state and can be replaced by a later decode window.

Station/configuration input is passed explicitly from `app_controller`, including:

```text
my callsign
my grid
Skip-TX1
max retry
CQ type / CQ FreeText
ad-hoc FreeText
Field Day exchange
```

Runtime beacon OFF/EVEN/ODD belongs to the controller-side TX lifecycle rather than persisted AutoSeq configuration.

## 7. Output contracts

AutoSeq exposes typed output without transferring storage ownership.

### QSO view

A read-only compact snapshot for UI/status consumers:

```text
dxcall
state
derived next TX
TX parity
retry state
active/inactive marker where relevant
```

The T UIScreen renders this view. It does not inspect internal queue storage. AS-3 projects caller-owned active snapshots into `UiModel.tx_lines[]`, with the model sized for the full 30-entry active queue and six visible entries per ADV page.

### TX intent

AS-7 implements `AutoSeqTxIntent` as a fixed-size semantic snapshot of what the queue head wants sent:

```text
QSO / CQ / FreeText intent type
TX1..TX6 semantic kind where applicable
CQ type
target/station callsign and grid facts
report or Field Day exchange facts
offset_hz
slot parity
retry state
```

`AutoSeqTxIntent` does not key a transmitter and contains no platform-specific CAT/audio operation, device handle, waveform, or heap-owned string. `app_controller` consumes the snapshot at a valid slot boundary.

### Policy events

AS-6 converts V2 logging side effects into typed `AutoSeqLogEvent` eligibility. AS-7 captures that event at TX start, before the simulated completion/tick. AutoSeq performs no I/O and the event is not acknowledged until a logging owner reports a successful write.

## 8. Event ordering

The proven V2 single-threaded ordering is represented in V3 as:

```text
RX slot completes
    -> decode/build RxBatch
    -> app_controller processes AutoSeq input
    -> AutoSeq updates queue and exposes AutoSeqTxIntent
    -> later UTC slot boundary decides parity/execution eligibility
    -> controller records simulated TX start
    -> controller captures logging eligibility where applicable
    -> simulated TX completes
    -> app_controller calls auto_seq_tick() exactly once
```

Decode completion must not directly start TX. AutoSeq remains synchronous and deterministic unless concurrency is later proven necessary.

AS-4 makes the RX half concrete: `rx_emit_event()` still owns only RX assembly; after `batch_generation` changes, `app_controller_step_rx()` walks the completed batch exactly once and feeds eligible addressed messages to AutoSeq in decode order.

AS-5 adds user queue-control events through `AppAction`. AS-7 supplies the production source for `auto_seq_tick()`: simulated TX completion after a valid slot/parity start.

The controller-side `TxLifecycle` observes MiniShell UTC but AutoSeq does not. Its first observation only anchors the current slot. A valid scheduling edge requires the immediately adjacent next 15-second slot and an observation within its first second. Duplicate slots, wrong parity, missed slots, suspend gaps, and backward/large clock corrections cannot consume AutoSeq TX state.

## 9. Real fixture anchors

### Multi-CQ fixture

```text
tests/kfs16b12k.wav
```

Current 2x2 result:

```text
Linux  16 decoded messages
ADV    16 decoded messages on the established physical hardware run
CQ      8 messages
```

AS-3 selects all 16 messages through the real RX UIScreen action path. Only the eight factual resolved CQs enter AutoSeq:

```text
N4NJJ
AG6X
AE7KJ
W7RPS
N7REB
WN0KS
N5CH
KQ4PUG
```

With Skip TX1 off, all eight ordinary CQs start as `REPLYING` with `RPLY 0/3`. Their real T-screen projection is six entries on page 1 and two on page 2. All were received in the same slot, so they request the same opposite TX parity.

AS-5 reuses this queue to prove same-parity rotation and absolute-index drop behavior. One rotation moves `N4NJJ` from the head to the end of the eight-entry run. Dropping the rotated head and then the sole page-2 entry leaves six active rows and collapses T from 2 pages to 1.

AS-7 keeps deterministic WAV fixtures isolated from wall-clock TX: when `--rx-slot` supplies a synthetic slot ID, the production wall-clock TX step is disabled so CI cannot consume or reorder the test queue merely by crossing a real 15-second boundary.

AS-8 retains this Linux production integration, runs the same pure fixed AutoSeq equivalence suite in both Linux and ADV CI gates, and separately cross-builds the production ESP32-S3 ADV firmware. The ADV CI host test is not represented as a physical Cardputer UI run; the earlier physical 16-message decode remains the hardware anchor.

### Addressed-message V2 goldens

AS-4 and AS-5 reuse the pinned MiniFT8-V2 WAVs:

```text
W1ABC K9XYZ FN42
W1ABC K9XYZ -12
W1ABC K9XYZ RR73
```

With W1ABC configured as the local station and no manual RX selection:

```text
fresh TX1/grid       -> K9XYZ RPRT 0/3
fresh TX2/report     -> K9XYZ RRPT 0/3
unknown late RR73    -> ignored, no QSO context
```

A two-slot WAV containing `FN42` followed by `-12` proves active-context matching: the second completed batch advances the existing K9XYZ context to `RRPT 0/3`, and the T screen contains exactly one K9XYZ entry.

AS-5 adds a second two-slot sequence, `-12` followed by `RR73`. After the first slot creates `K9XYZ RRPT`, dropping it from T parks the context in the inactive zone. The next addressed RR73 reactivates that same context and advances it to `SOFF`, again with exactly one K9XYZ entry.

No physical TX occurs during these tests.

## 10. Staged implementation

### AS-0 — Plan, reference freeze, boundary audit

Status: **COMPLETE**

Exit criteria:

```text
[x] V2 AutoSeq reference commit pinned
[x] V2 behavior declared oracle
[x] V3 ownership map locked
[x] current codebase boundary gaps identified
[x] no AutoSeq implementation code mixed into planning
```

### AS-1 — Complete AutoSeq input boundary

Status: **COMPLETE**

Purpose: supply the factual/context inputs V2 AutoSeq already relies on, without implementing QSO policy yet.

Completed work:

```text
ConfigService owns persisted station callsign/grid
app_controller injects local callsign into RxResultBuilder
RxMessage carries factual RX SNR and audio offset Hz
RX selection action carries absolute decoded-message index
```

### AS-2 — Pure compact AutoSeq core

Status: **COMPLETE**

Purpose: structurally port V2 AutoSeq into fixed C data with explicit ownership.

Completed work:

```text
apps/ft8/src/auto_seq/ added
fixed 30-entry active/inactive queue
56-byte host QsoContext
1712-byte host AutoSeq owner
next TX derived from state
no heap
no MiniShell
no UI
no file/log/radio calls
prototype qso_scheduler removed
```

Pure unit tests cover state progression, retry/inactive/reactivation behavior, priority/rotation/drop controls, reincarnation guards, queue bounds, oldest-inactive eviction, and the pinned V2 full-boundary edge. See `as-2-auto-seq-core.md`.

### AS-3 — CQ selection + real multi-QSO T screen

Status: **COMPLETE**

Purpose: connect the proven RX result boundary to AutoSeq manually, still with no TX.

Completed work:

```text
1..6 on RX -> absolute APP_ACTION_SELECT_RX_MESSAGE
app_controller validates retained RxMessage
selected resolved factual CQ -> AutoSeq manual-touch behavior
non-CQ selection remains selection-only
AutoSeq QsoView -> UiModel TX lines
T screen pages the real 30-entry-capable queue six rows at a time
```

Golden integrated result:

```text
kfs.wav -> 16 messages -> select all 16 -> 8 factual CQ -> queue=8 -> T pages 6+2
```

See `as-3-cq-t-screen.md`.

### AS-4 — Automatic addressed-to-me progression

Status: **COMPLETE**

Purpose: feed completed RX batches into AutoSeq exactly where V2 automatically processes messages addressed to us.

Completed work:

```text
RxResultBuilder classifies factual ordinary QSO stages TX1..TX5
RxMessage carries compact qso_kind + report_db
app_controller processes each completed batch once, in decode order
ordinary CQ remains manual-only
resolved parse-OK is_to_me messages feed auto_seq_on_addressed_rx()
active context matching and inactive reactivation remain AutoSeq-owned
unknown TX3/TX4/TX5 reincarnation guards remain AutoSeq-owned
full all-active queue drops an unqueueable new decode non-fatally as V2 does
```

Production proof uses pinned V2 WAVs with no RX-line selection. Fresh TX1 and TX2 create the expected V2 states; a two-slot same-DX sequence advances one existing context; an unknown RR73 creates no context. `auto_seq.c` required no AS-4 change. See `as-4-addressed-progression.md`.

### AS-5 — Retry, priority, inactive, reactivation, queue controls

Status: **COMPLETE**

Purpose: expose and verify the existing V2 queue lifecycle without introducing TX execution.

Completed work:

```text
T 1..6 -> absolute APP_ACTION_DROP_TX_QSO
T Enter -> APP_ACTION_ROTATE_TX_QUEUE
app_controller supplies monotonic time for inactive parking
normal QSO drop preserves metadata in inactive zone
same-parity head rotation preserves V2 queue semantics
later addressed RX reactivates a parked QSO through the AS-4 boundary
kfs eight-CQ queue proves rotate + page-1/page-2 absolute drop behavior
AS-2 pure tests remain authoritative for retry/exhaustion, state priority,
capacity, oldest-inactive eviction and unknown-message guards
```

`auto_seq_tick()` remained intentionally unwired through AS-5 because it represents a completed-TX lifecycle event. No AutoSeq policy changes were made. See `as-5-queue-lifecycle.md`.

### AS-6 — CQ/Beacon, FreeText, Field Day and logging eligibility

Status: **COMPLETE**

Purpose: port V2 special AutoSeq semantics before introducing slot execution.

Completed work:

```text
short-lived CQ one-shot with fixed CQ type/configuration
ad-hoc FreeText one-shot with V2 priority/parity behavior
Skip-TX1 preserved for ordinary QSO use
ARRL Field Day factual RX classification and exchange retention
CQ FD intentionally starts at TX2 with local exchange
typed independent ADIF/Cabrillo eligibility + acknowledgement
station.txt semantic CQ/FreeText/FD fields round-trip
no heap, file I/O, clock, Audio, CAT, or RF dependency in AutoSeq
```

Beacon OFF/EVEN/ODD runtime scheduling remained deferred to AS-7 rather than becoming persisted AutoSeq state. See `as-6-special-behavior.md`.

### AS-7 — Slot/TX-intent lifecycle without physical TX

Status: **COMPLETE**

Purpose: create the controller-owned execution lifecycle that turns an AutoSeq semantic request into one correctly timed simulated transmission.

Completed work:

```text
AutoSeq queue head -> fixed semantic AutoSeqTxIntent
MiniShell UTC -> controller-side 15-second TxLifecycle
first observation anchors without transmitting
slot parity gates execution
same-slot duplicates cannot transmit twice
missed/late/backward/large clock changes re-anchor with no catch-up TX
simulated TX start captures typed logging eligibility
simulated completion calls auto_seq_tick() exactly once
runtime Beacon OFF/EVEN/ODD re-enqueues one-shot CQ only when idle
QSO/FreeText work preempts beacon generation
beacon mode change removes stale queued CQ
--rx-slot deterministic fixtures are isolated from wall-clock TX stepping
no Audio TX, CAT/Control, waveform, or RF operation added
```

The AS-7 unit suite covers semantic TX intents, slot-edge/parity safety, retries, logging timing, beacon lifecycle, and QSO preemption. See `as-7-tx-lifecycle.md`.

### AS-8 — Equivalence closure

Status: **COMPLETE**

Purpose: close the structural port against the pinned V2 source/host behavior set without introducing a new AutoSeq policy change.

Completed work:

```text
pinned V2 host scenario families mapped to explicit V3 tests
AS-8 pure-C regression suite added for deadlock/reincarnation/signoff cases
same AS-8 AutoSeq suite required in Linux and ADV CI gates
kfs 16-message / 8-CQ / T 6+2 production integration remains green on Linux
established physical ADV 2x2 fixture remains 16 exact decodes
production ESP32-S3 ADV firmware cross-build remains green
AutoSeq allocator calls rejected mechanically by platform-boundary test
T UIScreen remains a QsoView projection, not direct AutoSeq storage access
all intentional V2/V3 differences documented in as-8-equivalence.md
no accidental production AutoSeq difference found that required a policy change
```

Exit criteria:

```text
[x] V2 host behavior scenarios represented by V3 tests
[x] kfs 16/8 fixture queue test green on Linux
[x] same fixed queue/state core verified in ADV gate and production ADV cross-build
[x] no heap allocation attributable to AutoSeq
[x] T screen accurately projects queue state
[x] MiniShell/Linux/FT8 Reference/ADV CI green
[x] behavioral differences explicitly documented; none accidental found
```

See `as-8-equivalence.md`.

After AS-8, the structural AutoSeq port is complete. Behavioral improvements may begin only as separate measured changes.

## 11. Post-port improvement rule

The AS-1 through AS-8 structural port is closed. Do not silently fold later behavioral changes into the old equivalence baseline.

For each post-port AutoSeq behavior change, record:

```text
old V2/V3 baseline behavior
reason for change
new invariant
unit/scenario test
Linux result
ADV result where relevant
RAM/timing effect where relevant
```

This preserves a known-good starting point while allowing deliberate AutoSeq improvements to proceed independently.

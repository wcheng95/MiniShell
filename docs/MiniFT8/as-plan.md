# MiniFT8-V3 AutoSeq Plan

Status: **IN PROGRESS — AS-0 through AS-4 complete; AS-5 next**

AutoSeq is the current major MiniFT8-V3 block after decode RX. This phase is a structural port first: preserve the proven MiniFT8-V2 AutoSeq behavior while replacing old ownership, dynamic data structures, and cross-module coupling with explicit V3 boundaries.

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

During the port, V2 behavior is authoritative unless a V3 boundary/ownership rule requires a different interface. Behavioral improvements are recorded separately and deferred until equivalence is established.

One intentional representation simplification is already approved and implemented in AS-2:

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

## 2. Goals

The AS block will:

- preserve current V2 QSO progression and scheduling behavior;
- preserve active/inactive queue semantics and late-reply reactivation;
- preserve retry behavior, priority sorting, same-parity rotation, drop behavior, Skip-TX1, CQ/FreeText one-shots, Field Day behavior, and logging eligibility timing;
- move AutoSeq into one explicit `auto_seq` module with one owner;
- use fixed-size C data and no AutoSeq heap allocation;
- consume factual `RxMessage`/`RxBatch` data rather than UI strings;
- expose typed QSO views and TX intents rather than platform/radio operations;
- keep `app_controller` as the sole application coordinator;
- make the T UIScreen a visible testbench for multiple queued QSOs before TX exists;
- test the same behavior on Linux and ADV.

## 3. Non-goals during the port

Do not use the AS port to redesign:

- QSO state progression;
- retry counts or timeout policy;
- queue priority/fairness policy;
- inactive-QSO policy;
- CQ/beacon semantics;
- FreeText priority;
- Field Day sequencing;
- logging eligibility rules;
- TX waveform generation;
- CAT/control behavior;
- radio selection;
- decoder algorithms.

If an improvement is noticed, add it to the post-port AutoSeq improvement list and continue preserving V2 behavior.

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
        `--> future TX/logging modules
```

Hard rules:

- `auto_seq` does not call MiniShell APIs;
- `auto_seq` does not read `station.txt`;
- `auto_seq` does not call `ft8_engine` or `Ft8HashStore`;
- `auto_seq` does not render UI;
- `auto_seq` does not perform ADIF/Cabrillo/file I/O;
- `auto_seq` does not start radio/audio TX;
- `ui_shell` never owns or receives `QsoContext` pointers;
- `app_controller` is the only production coordinator between RX, AutoSeq, UI, future TX, and logging.

## 5. AutoSeq state storage

The current V2 queue capacity is retained:

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

The AS-2 `QsoContext` is fixed-size and contains QSO-lifetime facts only:

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

AS-2 represents the subset needed by the pure state owner as a normalized `AutoSeqRxEvent`. AS-3 maps selected resolved CQs from real `RxMessage` data into that event. AS-4 adds factual ordinary-QSO `qso_kind/report_db` metadata and maps completed `is_to_me` messages automatically in decode order.

AutoSeq copies only QSO-lifetime facts into its own context. It must never retain a pointer into `RxBatch`, because the RX batch belongs to the RX state and can be replaced by a later decode window.

Station/configuration input is passed explicitly from `app_controller`, including:

```text
my callsign
my grid
Skip-TX1
max retry
CQ/Field Day configuration as later required
```

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

A semantic request describing what AutoSeq wants sent, for example:

```text
message kind: TX1..TX5 / CQ / FreeText / FD exchange
target callsign
report/exchange facts
offset_hz
slot parity
QSO identity/generation if needed
```

`TxIntent` does not key a transmitter and does not contain platform-specific CAT/audio operations. Future TX code realizes it. This remains deferred.

### Policy events

Where V2 currently performs side effects such as logging callbacks, V3 preserves the same eligibility/timing semantics by emitting an AutoSeq event to `app_controller`. The logging owner performs actual I/O. This remains a later AS stage.

## 8. Event ordering

Preserve the proven V2 single-threaded ordering:

```text
RX slot completes
    -> decode/build RxBatch
    -> app_controller processes AutoSeq input
    -> AutoSeq updates queue and exposes TxIntent
    -> slot boundary later decides TX eligibility
    -> future TX completes
    -> app_controller tells AutoSeq TX completed/tick
```

Decode completion must not directly start TX. AutoSeq remains synchronous and deterministic unless concurrency is later proven necessary.

AS-4 makes the first two steps concrete: `rx_emit_event()` still owns only RX assembly; after `batch_generation` changes, `app_controller_step_rx()` walks the completed batch exactly once and feeds eligible addressed messages to AutoSeq in decode order.

## 9. Real fixture anchors

### Multi-CQ fixture

```text
tests/kfs16b12k.wav
```

Current 2x2 result:

```text
Linux  16 decoded messages
ADV    16 decoded messages
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

With Skip TX1 off, all eight start as `REPLYING` with `RPLY 0/3`. Their real T-screen projection is six entries on page 1 and two on page 2. All were received in the same slot, so they request the same opposite TX parity.

### Addressed-message V2 goldens

AS-4 reuses the pinned MiniFT8-V2 WAVs:

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
selected resolved factual CQ -> AutoSeq V2-equivalent manual-touch behavior
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

Status: **NEXT**

Port and test current V2 behavior for:

```text
state-priority ordering
same-parity rotation
retry counting/exhaustion
active -> inactive movement
late reply reactivation
inactive eviction/expiry
drop behavior
unknown mid-QSO guards
```

Use V2 scenarios/traces as the oracle. No policy improvements yet.

### AS-6 — CQ/Beacon, FreeText, Field Day and logging eligibility

Port current V2 special behavior:

```text
short-lived CQ one-shot
FreeText one-shot and priority
Skip-TX1
ARRL Field Day sequencing
park-after-signoff behavior
ADIF/Cabrillo eligibility timing
```

Side effects are converted to typed events; behavior/timing remains equivalent.

### AS-7 — Slot/TX-intent lifecycle without physical TX

Add the app-controller event boundary that V2 requires:

```text
decode done -> AutoSeq update -> TxIntent latched
slot boundary -> intent becomes executable
simulated TX completion -> AutoSeq tick/advance
```

Use a fake TX completion event. Do not add Audio TX or Control yet.

### AS-8 — Equivalence closure

Run the V3 pure tests and integrated fixtures against the pinned V2 behavior set.

Exit criteria:

```text
V2 host behavior scenarios represented by V3 tests
kfs 16/8 fixture queue test green on Linux
same queue/state result on ADV
no heap allocation attributable to AutoSeq
T screen accurately projects queue state
all MiniShell/Linux/ADV CI green
behavioral differences explicitly documented; none accidental
```

After AS-8, the structural AutoSeq port is complete and behavioral improvements may begin as separate measured changes.

## 11. Post-port improvement rule

During AS-1 through AS-8, maintain a separate improvement TODO. Do not fold improvements into the structural port.

After equivalence is established, change one behavior at a time with:

```text
old V2 behavior
reason for change
new invariant
unit/scenario test
Linux result
ADV result where relevant
RAM/timing effect where relevant
```

This preserves a known-good starting point while still allowing the many AutoSeq improvements already anticipated for V3.

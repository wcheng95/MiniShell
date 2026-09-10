# MiniFT8-V3 AutoSeq Plan

Status: **PLANNED — implementation not started**

AutoSeq is the next major MiniFT8-V3 block after decode RX. This phase is a structural port first: preserve the proven MiniFT8-V2 AutoSeq behavior while replacing old ownership, dynamic data structures, and cross-module coupling with explicit V3 boundaries.

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

One intentional representation simplification is already approved:

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

Keep the current V2 queue capacity initially:

```text
AUTO_SEQ_MAX_QUEUE = 30
```

Preserve V2's single-array active/inactive layout:

```text
index 0                                              index 29
+----------------------+-------------+----------------------+
| active zone          | free space  | inactive zone        |
| grows ->             |             |              <- grows|
+----------------------+-------------+----------------------+
       active_count                  inactive_start
```

The V3 `QsoContext` uses fixed-size fields. Exact layout is finalized in AS-2 after `sizeof` measurement, but the intended shape is:

```c
QsoState state;
char dxcall[FT8_PROTOCOL_CALL_CAP];
char dxgrid[...];
int16_t offset_hz;
int8_t snr_tx;
int8_t snr_rx;
uint8_t retry_count;
uint8_t retry_limit;
uint8_t flags;
int64_t inactive_since_ms;
/* compact fixed Field Day metadata as required by V2 behavior */
```

Use a plain integer flag word/mask rather than C implementation-defined bit-fields. Candidate flags include active/logged/Cabrillo/FD/parity/park-after-signoff/FreeText as required by the V2 behavior model.

Do not store canonical display text as authoritative QSO state. Do not store `next_tx`; derive it from `state`.

Target: keep each context small and deterministic. Add compile-time size assertions once the exact structure is chosen. Do not optimize away fields whose V2 semantics are not yet proven redundant.

## 6. Input contracts

AutoSeq consumes factual MiniFT8 data only.

For a selected RX message it needs, as applicable:

```text
slot_id
protocol type
is_cq
is_to_me
call_to
call_de
extra/grid/report fields
frequency/offset
RX SNR
resolved/unresolved hash status
```

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

AutoSeq exposes three kinds of typed output.

### QSO view

A read-only compact snapshot for UI/status consumers:

```text
dxcall
state
TX parity
retry state
active/inactive marker where relevant
```

The T UIScreen renders this view. It does not inspect internal queue storage.

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

`TxIntent` does not key a transmitter and does not contain platform-specific CAT/audio operations. Future TX code realizes it.

### Policy events

Where V2 currently performs side effects such as logging callbacks, V3 preserves the same eligibility/timing semantics by emitting an AutoSeq event to `app_controller`. The logging owner performs actual I/O.

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

## 9. Real fixture anchor

Use the existing production fixture:

```text
tests/kfs16b12k.wav
```

Current 2x2 result:

```text
Linux  16 decoded messages
ADV    16 decoded messages
CQ      8 messages
```

The first integrated queue test uses the eight real CQs. Selecting them one by one should create eight independent QSO contexts:

```text
select CQ #1 -> queue 1
select CQ #2 -> queue 2
...
select CQ #8 -> queue 8
```

All eight were received in the same slot, so they request the same opposite TX parity. Only one is eligible per matching TX slot; preserve V2 queue priority/rotation behavior rather than inventing new scheduling.

With the ADV six-line T UIScreen, eight queued entries naturally exercise paging as 6 + 2.

No physical TX occurs during this test.

## 10. Staged implementation

### AS-0 — Plan, reference freeze, boundary audit

Status: **COMPLETE when `as-plan.md` and `as-boundary-audit.md` merge.**

Exit criteria:

```text
[ ] V2 AutoSeq reference commit pinned
[ ] V2 behavior declared oracle
[ ] V3 ownership map locked
[ ] current codebase boundary gaps identified
[ ] no AutoSeq implementation code mixed into planning
```

### AS-1 — Complete AutoSeq input boundary

Purpose: supply the factual/context inputs V2 AutoSeq already relies on, without implementing QSO policy yet.

Work:

```text
ConfigService owns persisted station callsign/grid
app_controller injects local callsign into RxResultBuilder
RxMessage carries factual RX SNR
RX selection action carries absolute decoded-message index
```

Exit criteria:

```text
station identity has one owner
is_to_me works from configured station identity
selected message resolves through app_controller to retained RxBatch
RX SNR is factual input, not estimated inside AutoSeq
no QSO policy added yet
```

### AS-2 — Pure compact AutoSeq core

Purpose: structurally port V2 AutoSeq into fixed C data with explicit ownership.

Work:

```text
new apps/ft8/src/auto_seq/
fixed 30-entry active/inactive queue
compact QsoContext
next TX derived from state
no heap
no MiniShell
no UI
no file/log/radio calls
```

Port V2 state transitions and helper behavior without improvement.

Exit criteria:

```text
pure unit-testable module
sizeof(QsoContext) and sizeof(AutoSeq) measured
queue bounds statically enforced
V2 state/queue behavior represented without dynamic strings
```

At this point remove the prototype `qso_scheduler`; do not keep two owners of AutoSeq settings/policy.

### AS-3 — CQ selection + real multi-QSO T screen

Purpose: connect the proven RX result boundary to AutoSeq manually, still with no TX.

Work:

```text
1..6 on RX -> absolute APP_ACTION_SELECT_RX_MESSAGE
app_controller validates selected RxMessage
CQ selection -> AutoSeq V2-equivalent manual-touch behavior
AutoSeq QsoView -> UiModel TX lines
T screen pages real queue
```

Golden integrated test:

```text
kfs.wav -> 16 messages -> 8 CQ -> select all 8 -> queue=8 -> T pages 6+2
```

### AS-4 — Automatic addressed-to-me progression

Purpose: feed completed RX batches into AutoSeq exactly where V2 automatically processes messages addressed to us.

Rules:

```text
ordinary CQ: no automatic reply
selected CQ: manual queue insertion
is_to_me: automatic AutoSeq processing
```

Preserve V2 duplicate/matching/reply state behavior.

### AS-5 — Retry, priority, inactive, reactivation, queue controls

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

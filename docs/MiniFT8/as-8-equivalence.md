# MiniFT8-V3 AS-8: V2 Equivalence Closure

Status: **COMPLETE**

AS-8 closes the structural AutoSeq port. No new production AutoSeq policy was introduced in this stage. The purpose was to make the pinned MiniFT8-V2 behavior set explicit, verify the V3 fixed-size C owner against it, retain the existing real integration fixtures, and document every deliberate V2/V3 difference.

## Behavioral oracle

The comparison reference remains:

```text
repository  wcheng95/Mini-FT8
commit      491e757ae6b1e4cfd2b9a6ba10f48b35643849e0
source      main/autoseq.cpp
            main/autoseq.h
            host_mock/*
```

The pinned V2 host scenarios were reviewed together with the pinned source. Comments in historical JSON scenarios are not treated as oracle output when they describe the bug that the scenario was intended to reproduce; the repaired V2 source behavior is authoritative.

## AS-8 regression coverage

`tests/ft8_auto_seq_as8_test.c` is a compact pure-C equivalence suite using the same `auto_seq.c` source that is linked into MiniFT8-V3.

The V2 behavior set maps to V3 coverage as follows:

| Pinned V2 scenario | V3 proof |
| --- | --- |
| ordinary QSO progression | AS-8 normal-QSO progression plus AS-2/AS-4 tests |
| report deadlock | REPORT + plain TX2 advances to ROGER_REPORT/TX3 |
| inactive reactivation deadlock | exhausted REPORT context parks, late TX1 reactivates it, derived next TX remains TX2 |
| report reincarnation | unknown fresh TX3 is rejected |
| Rogers/signoff reincarnation | unknown fresh TX4/TX5 is rejected; terminal QSO cannot be recreated by a late signoff |
| ad-hoc FreeText | one-shot FreeText preempts an active QSO, inherits queue parity, then the QSO resumes |
| CQ/beacon lifecycle | AS-6 one-shot CQ tests plus AS-7 controller-side beacon lifecycle/preemption tests |
| Field Day exchange progression | AS-6 tests plus AS-8 FD progression |
| Field Day TX4/TX5 logging | typed eligibility is emitted once and acknowledged independently |
| Field Day late signoff/reentry | parked SIGNOFF can answer a late RR73 without duplicate logging |
| Field Day signoff retry | retry/reentry preserves one-log semantics |
| Field Day manual CQ touch | covered with the deliberate V3 TX2 rule documented below |

The older `tests/ft8_auto_seq_as2_test.c` remains the broad fixed-owner test suite for queue capacity, priority/sorting, retry exhaustion, inactive eviction/reactivation, rotation/drop, Skip-TX1, CQ/FreeText, Field Day, and logging eligibility. AS-8 does not replace it.

## Integrated production proof

The real production fixture remains:

```text
tests/kfs16b12k.wav
profile       ADV
time_osr     2
freq_osr     2
RX slot       12345
```

The production Linux path still proves:

```text
16 decoded messages
 8 factual resolved CQs
 8 AutoSeq active entries
all eight -> RPLY 0/3
T page 1 -> 6 rows
T page 2 -> 2 rows
```

The eight queue entries remain, in decode order:

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

The same integration also proves same-parity rotation, absolute-index drop from both T pages, and page collapse from two pages to one.

A previous physical Cardputer ADV run of the same 2x2 `kfs` fixture produced the same 16 decoded messages. AS-8 adds the pure AutoSeq equivalence binary to the ADV CI gate and cross-builds the production ESP32-S3 firmware from the same source. AS-8 does **not** claim that GitHub-hosted CI is a physical Cardputer UI run; the physical 16-message decode remains the hardware anchor, while the fixed platform-independent AutoSeq state owner is verified identically in the Linux and ADV gates.

## Heap and bounded-state proof

AutoSeq remains a caller-owned fixed-size C object:

```text
AUTO_SEQ_MAX_QUEUE = 30
sizeof(QsoContext) <= 64 bytes
sizeof(AutoSeq)    <= 2048 bytes
```

The established x86-64 measurements remain:

```text
sizeof(QsoContext) =   56 bytes
sizeof(AutoSeq)    = 1712 bytes
```

AS-8 strengthens the boundary mechanically: `tests/ft8_platform_boundary.py` now rejects allocator calls in `apps/ft8/src/auto_seq/`, including `malloc`, `calloc`, `realloc`, `aligned_alloc`, `free`, `strdup`, and `asprintf`.

AutoSeq therefore owns no heap allocation. Its repeated QSO state remains bounded and compact, which is important with the production 2x2 RX RAM budget.

## Deliberate V2/V3 differences

These differences are intentional and are **not** equivalence failures.

### 1. Field Day CQ reply starts at TX2

Pinned V2 manual CQ handling applied the global Skip-TX1 setting even when the selected message was CQ FD. With Skip-TX1 off, V2 therefore began at TX1.

MiniFT8-V3 deliberately changes that rule: replying to CQ FD skips TX1 and immediately sends the local Field Day exchange as TX2. This is the canonical V3 Field Day behavior.

### 2. `next_tx` is derived, not mutable state

Pinned V2 stored both QSO state and `next_tx`. Its repaired state machine had to restore `next_tx` on no-op/reentry paths to avoid stale `TX_NONE` deadlocks.

V3 stores the QSO state only and derives normal-QSO TX meaning:

```text
REPLYING      -> TX1
REPORT        -> TX2
ROGER_REPORT  -> TX3
ROGERS        -> TX4
SIGNOFF       -> TX5
```

This is a representation/robustness improvement with the same normal QSO semantics and removes the stale-`next_tx` failure class structurally.

### 3. Fixed owner replaces V2 singleton/dynamic representation

V2 used C++ singleton/global state and dynamic strings/containers. V3 uses one explicit caller-owned fixed C `AutoSeq` object with bounded arrays and compact fields. Ownership changed; the preserved QSO policy did not.

### 4. Logging is an event contract, not AutoSeq I/O

V2 invoked ADIF/Cabrillo callbacks around TX4/TX5 emission. V3 preserves that semantic trigger but exposes a typed `AutoSeqLogEvent` at TX start. A separate logging owner must persist and acknowledge ADIF/Cabrillo independently. AutoSeq performs no filesystem operation.

### 5. Beacon timing belongs to the controller

V3 keeps one-shot CQ policy in AutoSeq but places runtime Beacon OFF/EVEN/ODD scheduling and UTC slot observation in `app_controller`/`TxLifecycle`. This preserves CQ/QSO/FreeText priority semantics without giving AutoSeq a clock or platform dependency.

### 6. T presentation is V3 UI, not V2 screen geometry

V3 uses the 20x7 UI and displays six T rows per page. The old V2 visual geometry is not reproduced. Queue order, state, retry count, parity, and controls are the semantic equivalence target.

## CI closure

The first complete AS-8 implementation head passed:

```text
Linux          #591  PASS
FT8 Reference  #97   PASS
ADV            #331  PASS
```

Linux runs both the existing full production integration and the new AS-8 equivalence binary. The ADV workflow runs the same pure AutoSeq equivalence binary in its gate and separately cross-builds the Cardputer ADV ESP32-S3 firmware.

The final documentation head is required to pass the same three gates before merge.

## Result

AS-8 found no accidental production AutoSeq behavior difference that required a source-policy change. The structural port is closed through AS-8.

Future AutoSeq behavior changes are no longer part of the V2 port. Each should be introduced independently with the old behavior, reason for change, new invariant, tests, Linux result, ADV result where relevant, and RAM/timing effect where relevant.

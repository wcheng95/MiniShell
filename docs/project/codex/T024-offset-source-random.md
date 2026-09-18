# T024 — station.txt offset source / Random TX offset

Status: READY

## Architect intent

Make the architect's existing station setting actually work:

```text
offset_src=0
```

The required first behavior is V2-compatible **Random** TX offset selection.

T023 operator controls are already hardware-validated and the T022 physical
QMX FT8 path is already on-air decodable. This task must not disturb that path.

After T024, the architect intends to move the Linux build to the WinBook TW700,
so this task should leave a clean portable Linux baseline with no pc-1-specific
random implementation.

## Branch dependency

T024 is stacked on completed T023:

```text
codex/T022-linux-qmx-first-qso
  -> codex/T023-cq-beacon-ui
  -> codex/T024-offset-source-random
```

Base:

```text
2c0898e41adf10413dbe3a9c4dc63a23dabda745
```

Do not rebase onto old main while T022/T023 are still stacked.

## V2 compatibility contract

Pinned MiniFT8-V2:

```text
wcheng95/Mini-FT8
491e757ae6b1e4cfd2b9a6ba10f48b35643849e0
```

V2 station enum:

```text
offset_src=0   RANDOM
offset_src=1   CURSOR / Fixed
offset_src=2   RX
```

V2 resolver semantics:

```text
Fixed:
    use configured fixed offset

RX:
    for non-CQ with valid received offset, use received offset
    for CQ, use Random

Random:
    roll a new offset in [500, 2500] Hz
```

The V2 implementation uses:

```text
500 + random % 2001
```

T024 must preserve the numeric station-file meanings.

## Objective

Add V3 offset-source configuration and resolve the actual T021 plan base frequency
inside the application/controller boundary before physical TX.

Required result for the architect's current station file:

```text
callsign=AG6AQ
grid=CM97
offset_src=0
```

A new physical CQ/POTA attempt must choose:

```text
base_hz ∈ [500, 2500]
```

and that exact resolved value must be used consistently by:

- T021 `Ft8TxPlan.base_hz`;
- T020 QMX TA commands;
- RxTxLog T record;
- any controller snapshot of the attempted TX.

Do not change the 6.25 Hz tone spacing or tone indices.

## Important current V3 issue

Before T024, `offset_src=0` is unknown to ConfigService and ignored.

Also, any later V3 config save (for example changing CQ <-> CQ POTA in T023)
serializes only known fields and therefore may remove the manually added unknown
`offset_src=0` line.

T024 must fix both parsing **and preservation via serialization**.

## Source of truth

Read before editing:

```text
AGENTS.md
docs/MiniFT8/development.md
docs/MiniFT8/architecture.md
docs/project/codex/T022-linux-qmx-first-qso.md
docs/project/codex/T023-cq-beacon-ui.md

apps/ft8/src/config_service/config_service.[ch]
apps/ft8/src/app_controller/app_controller_internal.h
apps/ft8/src/app_controller/app_controller_tx_physical.c
apps/ft8/src/app_controller/app_controller_tx.c
apps/ft8/src/auto_seq/auto_seq.c
apps/ft8/src/auto_seq/auto_seq_tx_intent.c
apps/ft8/src/tx_encoder/tx_encoder.[ch]
apps/ft8/src/log_service/log_service.[ch]
```

Pinned V2 references:

```text
main/station_types.h
main/main.cpp
```

Particularly inspect:

```text
OffsetSrc
resolve_tx_offset()
offset_src load/save
offset fixed value load/save
```

## Part A — ConfigService offset source

Add a V3 config enum/value with exactly these stable numeric meanings:

```c
FT8_OFFSET_RANDOM = 0
FT8_OFFSET_FIXED  = 1
FT8_OFFSET_RX     = 2
```

Naming may differ, numeric values may not.

Add to `ConfigService`:

```text
offset_src
fixed_offset_hz
```

### Defaults

Use:

```text
offset_src=0
offset=1500
```

Thus a missing `offset_src` key defaults to Random, matching current V2's
default source.

### Parsing

Recognize:

```text
offset_src=0
offset_src=1
offset_src=2
offset=<integer>
```

Reject invalid `offset_src` values rather than clamping silently.

For fixed offset, accept a safe FT8 audio range:

```text
300..2700 Hz
```

This matches the bounded T020 diagnostic range and leaves the full FT8 tone set
within a normal SSB audio passband.

If `offset` is absent, default 1500.

### Serialization

Always serialize both:

```text
offset_src=<0|1|2>
offset=<fixed offset>
```

This guarantees the architect's manually added `offset_src=0` survives T023
CQ-type config saves.

No UI for offset source is required in T024.

## Part B — portable Random generator

Do not call:

- ESP-IDF `esp_random()`;
- Linux `getrandom()`;
- POSIX `random()`;
- libc global `rand()`;
- platform-specific APIs.

MiniFT8 application code must remain portable.

Add a tiny application-owned deterministic PRNG helper/state, for example
xorshift32, PCG-like 32-bit state, or another simple non-cryptographic generator.

Requirements:

- no heap;
- no global libc PRNG state;
- deterministic unit-testable step function;
- nonzero state invariant if the algorithm requires it;
- seed at controller initialization from available MiniShell time observations
  (UTC and/or monotonic time);
- include a fixed compile-time mixing constant so all-zero/missing time does not
  leave a degenerate generator;
- this is RF-placement randomness, not cryptography.

A pure helper module such as:

```text
apps/ft8/src/tx_offset/
    tx_offset.c
    tx_offset.h
```

is preferred if it keeps controller code simple.

## Part C — resolution ownership

Do not put Random policy into:

- AutoSeq;
- tx_encoder;
- radio_control;
- MiniShell core/platform.

Ownership:

```text
AutoSeqTxIntent
    contains semantic/reference offset

AppController TX preparation
    resolves source policy -> actual base_hz

T021 Ft8TxPlan
    snapshots actual base_hz

T020 radio_control
    transmits what plan requests
```

The T021 encoder remains pure and unchanged.

## Part D — offset semantics

Implement a pure resolver equivalent to:

```text
resolve(offset_src, fixed_offset, intent, rng)
```

### RANDOM (0)

For every newly armed physical TX attempt:

```text
500 <= resolved_hz <= 2500
```

Use inclusive 2001-value range.

This applies to:

- CQ;
- CQ POTA;
- normal QSO replies;
- retries;
- free text.

A retry/new attempt may receive a new Random value, matching the V2 arm-time
behavior.

Once the physical T021 plan is created for one attempt, its `base_hz` is
immutable for all 79 symbols.

Do not re-roll during a message.

### FIXED (1)

Use:

```text
fixed_offset_hz
```

exactly.

No UI is required; editing station.txt is sufficient.

### RX (2)

For a non-CQ QSO intent with a valid received/reference offset in the supported
audio range:

```text
resolved_hz = intent.offset_hz
```

For CQ/beacon:

```text
resolved_hz = Random 500..2500
```

For free text with no meaningful RX source, use Random unless the existing
semantic context clearly carries a valid RX/QSO offset.

Record the exact chosen rule in the task handoff and tests.

If a received/reference offset is outside the safe 300..2700 range, fall back to
Random rather than transmitting an invalid tone.

## Part E — physical TX integration

In T022 `app_controller_step_tx()`, the current physical path:

```text
AutoSeqTxIntent intent
ft8_tx_encode(intent, &plan)
```

must become conceptually:

```text
AutoSeqTxIntent intent
resolved = resolve_tx_offset(...)
intent.offset_hz = resolved
ft8_tx_encode(&intent, &plan)
```

Use a local/snapshotted intent.

Do not mutate AutoSeq queue storage just to store the resolved Random value.

After successful resolution:

- `app->tx.last_intent` should reflect the actual resolved offset used on air,
  not the stale semantic/default value;
- `app->tx.plan.base_hz` must equal it;
- RxTxLog T line must use it;
- QMX TA sequence must derive from it.

## Part F — reply behavior

Keep AutoSeq's received offset fact intact.

When a received CQ/reply has:

```text
event.offset_hz = 1234
```

AutoSeq may keep that factual offset.

Then:

```text
offset_src=2 (RX)
    -> physical plan base 1234

offset_src=0 (RANDOM)
    -> physical plan base random 500..2500
```

Changing source policy must not overwrite the retained RX fact in the QSO context.

## Part G — simulated no-CAT path

Do not destabilize existing AS-7 simulation tests.

The Random RF placement policy is primarily a physical-plan concern.

No-CAT simulation may continue using semantic/default `intent.offset_hz` unless
a simple shared resolver can be introduced without making deterministic simulation
tests flaky.

Never let randomization make existing pure/no-radio tests nondeterministic.

## Part H — RxTxLog

The T022 T record already logs `plan.base_hz`.

Prove that with:

```text
offset_src=0
CQ POTA AG6AQ CM97
```

the T line's offset field is the same random base used by the plan/CAT path.

Example shape:

```text
T [...] CQ POTA AG6AQ CM97 1873
```

Do not require a specific value in hardware.

## Part I — safety/range

Random base is limited to 500..2500 Hz.

With FT8 maximum tone offset:

```text
7 * 6.25 = 43.75 Hz
```

the highest TA frequency is therefore <= 2543.75 Hz.

Tests must prove every Random base produces all FT8 tones within:

```text
500.00 .. 2543.75 Hz
```

for Random mode.

## Tests

### ConfigService

Prove:

- missing offset keys -> Random + 1500;
- `offset_src=0` -> Random;
- `offset_src=1` -> Fixed;
- `offset_src=2` -> RX;
- invalid values fail parse;
- offset 300 and 2700 accepted;
- offset 299/2701 rejected;
- serialize/parse roundtrip;
- T023 CQ type save preserves `offset_src=0` and `offset=...`.

### PRNG/helper

Use a fixed seed and prove a fixed deterministic sequence.

Prove many generated values are:

```text
500..2500 inclusive
```

and both boundary mapping math and modulo range are correct.

Do not assert statistical quality beyond basic deterministic/range sanity.

### Physical CQ integration

With fixture:

```text
callsign=AG6AQ
grid=CM97
cq_type=2
offset_src=0
```

and POTA beacon enabled:

- physical plan text is `CQ POTA AG6AQ CM97`;
- plan base is 500..2500;
- T record offset equals plan base;
- first TA = plan base + first tone*6.25;
- all later TA derive from the same immutable base;
- next independent CQ attempt advances PRNG and resolves a fresh plan once.

Do not require two consecutive values to differ; PRNG correctness comes from
deterministic sequence tests.

### RX source integration

For received factual offset 1234:

```text
offset_src=2 -> base 1234
offset_src=0 -> base in 500..2500
offset_src=1, offset=1600 -> base 1600
```

Prove the AutoSeq context still retains 1234 after Random/Fixed physical plans.

### Failure/retry

If RT append, Audio pause, CAT begin, or tone write fails:

- Random resolution may already have advanced PRNG state;
- semantic AutoSeq intent remains unconsumed as in T022;
- a later retry may roll a new random base;
- no requirement to reproduce the failed attempt's random value.

Document this explicitly.

### T022/T023 regressions

Keep all:

- physical TX;
- RX recovery;
- RxTxLog;
- CQ/POTA;
- OFF/EVEN/ODD;
- station identity;
- failure cleanup

tests green.

## Non-goals

Do not add:

- O->TX offset UI;
- Random/RX/Fixed UI menu;
- fixed-offset editor;
- waterfall cursor controls;
- new MiniShell random API;
- platform-specific entropy;
- audio TX;
- FT4;
- changes to T021 encoding;
- changes to QMX CAT formatting;
- WinBook-specific code.

WinBook/TW700 testing comes after this task.

## Acceptance criteria

- [ ] `offset_src=0` is parsed and means Random;
- [ ] numeric V2 meanings 0/1/2 are preserved;
- [ ] `offset_src` survives station config saves;
- [ ] `offset` fixed value parses/serializes;
- [ ] Random is 500..2500 inclusive;
- [ ] Random implementation is portable/application-owned;
- [ ] no platform/libc global PRNG dependency;
- [ ] Random is resolved once per physical TX attempt;
- [ ] immutable plan uses resolved value for all 79 tones;
- [ ] RxTxLog T record uses same resolved value;
- [ ] Fixed uses configured offset;
- [ ] RX uses received offset for non-CQ when valid;
- [ ] RX CQ falls back to Random;
- [ ] AutoSeq retained factual RX offset is not overwritten;
- [ ] T022 physical TX tests remain green;
- [ ] T023 CQ/beacon tests remain green;
- [ ] Linux full CTest passes;
- [ ] units pass;
- [ ] architecture checks pass;
- [ ] sanitizer coverage for pure resolver/PRNG passes;
- [ ] real ADV build passes;
- [ ] no unrelated changes.

## Hardware/operator acceptance

Use the existing pc-1/QMX setup first.

Station file:

```text
callsign=AG6AQ
grid=CM97
cq_type=2
offset_src=0
rxtx_log=1
```

Launch T024 and enable:

```text
O -> 4
CQ POTA
EVEN or ODD
```

Observe several TX attempts.

PASS requires:

- CQ POTA still transmits normally;
- RT T lines show base offsets within 500..2500;
- at least two attempts can be observed without any stuck TX/RX issue;
- received spots remain decodable if available;
- no regression in post-TX RX.

A completed QSO is welcome but not required specifically for T024.

After T024 hardware acceptance, the same Linux feature set is ready to be exercised
on WinBook/TW700.

## Automated gate

Run:

```bash
git status --short

cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure

cmake -S tests/unit -B /tmp/T024-build-unit
cmake --build /tmp/T024-build-unit -j"$(nproc)"
ctest --test-dir /tmp/T024-build-unit --output-on-failure

PYTHONDONTWRITEBYTECODE=1 python3 tests/app_dependency_boundary.py . ft8
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_platform_boundary.py . ft8
PYTHONDONTWRITEBYTECODE=1 python3 tests/ft8_platform_boundary.py .
PYTHONDONTWRITEBYTECODE=1 python3 tests/serial_protocol_boundary.py .
PYTHONDONTWRITEBYTECODE=1 python3 tests/architecture_rules.py .

source ~/projects/esp-idf/export.sh
idf.py -C platform/adv build

git diff --check
```

No Actions wait.

## Branch workflow

Use:

```text
codex/T024-offset-source-random
```

Codex:

1. implement portable V2-compatible offset-source config/resolution;
2. keep T022/T023 behavior otherwise unchanged;
3. run all local gates;
4. set Status to REVIEW;
5. record exact resolver semantics and PRNG seeding;
6. commit and push one reviewable implementation commit;
7. return SHA;
8. no PR;
9. no Actions wait;
10. no RF testing by Codex.

## Codex implementation notes

### Implementation summary

### Files changed

### Config behavior

### Random/PRNG model

### Offset-resolution semantics

### T022/T023 invariants preserved

### Tests run

### Hardware validation still required

### Known limitations / risks

### Commit

## Supervisor review

Review exact diff, particularly:

- no entropy/platform leakage;
- station numeric compatibility;
- one-time resolution per physical attempt;
- no mutation of AutoSeq received offset facts;
- plan/RT/CAT consistency;
- no T022 timing/RX cleanup regressions.

## Architect test result

Record:

- station offset settings;
- several RT T offsets;
- observed on-air behavior;
- post-TX RX recovery;
- any QSO/spot evidence.

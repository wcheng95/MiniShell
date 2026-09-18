# T023 — O -> 4 CQ / Beacon controls

Status: REVIEW

## Architect intent

Add only the two CQ/beacon controls needed to operate MiniFT8-V3 on the air.

Current T022 physical TX is already proven decodable on real QMX hardware and
RxTxLog is working. The missing operator control is the O-screen CQ/beacon submenu.

Required UI path:

```text
O
 -> 4  CQ / Beacon
      -> 1  CQ Type
      -> 2  Beacon
```

Line 1 exposes exactly two choices:

```text
CQ
CQ POTA
```

Line 2 exposes exactly three choices:

```text
OFF
EVEN
ODD
```

Do not add any other UI work in this task.

## Branch dependency

T023 is intentionally stacked on the current T022 TESTING branch:

```text
codex/T022-linux-qmx-first-qso
    -> codex/T023-cq-beacon-ui
```

Base commit:

```text
bb0de152681a85ee4684dcbde5349fdb1146212b
```

Do not rebase onto older `main` while T022 is still under hardware acceptance.

## Existing plumbing to reuse

The underlying policy already exists:

- `ConfigService.cq_type`
- `config_service_set_cq_type()`
- `AutoSeqCqType`
- `auto_seq_set_cq()`
- `TxLifecycle`
- `TxBeaconMode`
- `app_controller_set_beacon_mode()`
- `app_controller_get_beacon_mode()`

T021 already encodes:

```text
CQ <CALL> <GRID>
CQ POTA <CALL> <GRID>
```

T022 already performs physical slot-anchored QMX TX.

T023 is therefore UI/controller wiring, not TX-engine work.

## Source of truth

Read before editing:

```text
AGENTS.md
docs/MiniFT8/ui.md
docs/MiniFT8/development.md
docs/project/codex/T022-linux-qmx-first-qso.md

apps/ft8/include/ft8/app_types.h
apps/ft8/src/ui_shell/ui_shell.c
apps/ft8/src/ui_shell/ui_shell.h
apps/ft8/src/app_controller/app_controller.c
apps/ft8/src/app_controller/app_controller_tx.c
apps/ft8/src/app_controller/app_controller_internal.h
apps/ft8/src/config_service/config_service.[ch]
apps/ft8/src/auto_seq/
apps/ft8/src/tx_lifecycle/
```

## UI behavior

### Entry

From top-level O screen:

```text
O
4
```

enters the existing `UI_SUBMENU_O_CQ`.

The submenu must render real values instead of placeholders.

ADV example:

```text
O  20 12:34:56 1/1 0
>1 CQ Type: CQ
 2 Beacon: OFF
```

or:

```text
>1 CQ Type: CQ POTA
 2 Beacon: EVEN
```

Keep all text within the existing 20-column ADV constraints.

### Line 1 — CQ Type

Only these UI-visible values exist in T023:

```text
CQ
CQ POTA
```

Forward cycle:

```text
CQ -> CQ POTA -> CQ
```

Backward cycle is the reverse, which is the same toggle for two values.

Input behavior:

- pressing `1` while in O->4 cycles forward;
- Enter while line 1 is selected cycles forward;
- Right cycles forward;
- Left cycles backward.

Action must flow through `AppAction` to `AppController`.

Do not mutate ConfigService directly from ui_shell.

### Line 2 — Beacon

Values:

```text
OFF
EVEN
ODD
```

Forward:

```text
OFF -> EVEN -> ODD -> OFF
```

Backward:

```text
OFF <- EVEN <- ODD <- OFF
```

Input behavior:

- pressing `2` while in O->4 cycles forward;
- Enter while line 2 is selected cycles forward;
- Right cycles forward;
- Left cycles backward.

Use existing `app_controller_set_beacon_mode()`.

## UiModel additions

Expose enough factual state for rendering and action generation.

Add fields equivalent to:

```c
Ft8ConfigCqType cq_type;
TxBeaconMode beacon_mode;
```

Avoid making ui_shell depend directly on ConfigService or TxLifecycle headers if a
small UI-owned enum/value is cleaner.

The model must always reflect controller state after an action is applied.

## AppAction additions

Add explicit semantic actions equivalent to:

```text
APP_ACTION_SET_CQ_TYPE
APP_ACTION_SET_BEACON_MODE
```

Do not overload generic index/profile actions.

### CQ action handling

Controller behavior:

1. accept only CQ and POTA from this T023 UI action;
2. call `config_service_set_cq_type()`;
3. synchronize AutoSeq configuration so the next CQ intent uses the new type;
4. persist `station.txt` through the existing atomic config save path.

Expected persisted values:

```text
cq_type=0   # CQ
cq_type=2   # CQ POTA
```

Existing parser support for other CQ types remains untouched; they are simply not
exposed by this minimal UI.

If station save fails, report action failure rather than claiming the UI mutation
was committed.

### Beacon action handling

Controller behavior:

1. accept OFF/EVEN/ODD;
2. call `app_controller_set_beacon_mode()`;
3. beacon remains runtime-only;
4. do not add a station.txt key for beacon.

This preserves existing design: launch starts beacon OFF.

Changing beacon mode already removes a stale queued one-shot CQ through the
existing controller helper; preserve that behavior.

## Operational semantics

When:

```text
CQ Type = CQ
Beacon = EVEN
```

the physical path should produce one-shot intents on even slots:

```text
CQ AG6AQ CM97
```

When:

```text
CQ Type = CQ POTA
Beacon = ODD
```

the physical path should produce:

```text
CQ POTA AG6AQ CM97
```

on odd slots.

Station identity still comes from:

```text
/flash/ft8/station.txt
callsign=AG6AQ
grid=CM97
```

Do not hardcode either value.

### Queue priority

Preserve existing AutoSeq behavior:

- an active QSO/reply/free-text intent takes precedence over beacon CQ;
- beacon CQ is created only when no higher-priority intent exists;
- changing beacon OFF removes any stale pending one-shot CQ;
- normal QSO retries continue to use their assigned parity.

Do not create a separate beacon queue.

## Active-TX behavior

T022 freezes configuration/actions during active physical TX.

Preserve that rule.

If line 1 or 2 is operated while physical TX is active:

- do not mutate the active immutable plan;
- do not retarget the in-flight slot;
- existing controller action-freeze behavior may ignore the requested change.

No need to queue a deferred setting change in T023.

## Tests

### UI rendering

Prove O->4 renders:

```text
CQ Type: CQ
CQ Type: CQ POTA
Beacon: OFF
Beacon: EVEN
Beacon: ODD
```

with valid ADV 20-column output.

### UI input

Prove:

- O then 4 enters `UI_SUBMENU_O_CQ`;
- line 1 number/Enter/Left/Right emits only CQ-type actions;
- line 2 number/Enter/Left/Right emits only beacon actions;
- line 1 cycles only CQ <-> POTA;
- line 2 cycles OFF/EVEN/ODD with wrap;
- Back behavior remains unchanged.

### Controller CQ type

Starting from:

```text
callsign=AG6AQ
grid=CM97
cq_type=0
```

set POTA and prove:

- ConfigService becomes `FT8_CONFIG_CQ_POTA`;
- AutoSeq CQ config becomes POTA;
- station.txt contains `cq_type=2`;
- a generated CQ intent encodes through T021 as:
  ```text
  CQ POTA AG6AQ CM97
  ```

Set back to CQ and prove:

```text
CQ AG6AQ CM97
cq_type=0
```

### Controller beacon mode

Prove runtime sequence:

```text
OFF -> EVEN -> ODD -> OFF
```

and reverse sequence.

Prove:

- mode is not serialized to station.txt;
- application/controller initialization starts OFF;
- active higher-priority QSO prevents beacon CQ;
- correct parity produces CQ only when queue otherwise has no intent;
- wrong parity does not produce CQ.

### T022 regression

Keep physical-TX tests green.

At least one integration test should set:

```text
CQ Type = POTA
Beacon = EVEN or ODD
```

and verify the resulting physical T021 plan canonical text is:

```text
CQ POTA AG6AQ CM97
```

before the mocked T020 CAT path keys.

No real RF test is required for Codex.

## Non-goals

Do not add:

- CQ SOTA;
- CQ QRP;
- CQ FD;
- CQ FreeText;
- POTA-specific logging fields;
- park/reference entry;
- offset controls;
- Tune;
- free-text editor;
- RxTxLog UI toggle;
- station editor;
- new CAT behavior;
- TX timing changes;
- RX changes;
- AutoSeq state-machine changes;
- beacon persistence;
- new command-line options.

## Acceptance criteria

- [x] O->4 line 1 shows CQ or CQ POTA;
- [x] O->4 line 2 shows OFF/EVEN/ODD;
- [x] number keys 1/2 cycle corresponding value forward;
- [x] Enter cycles selected line forward;
- [x] Left/Right cycle selected value backward/forward;
- [x] CQ type changes via explicit AppAction;
- [x] CQ type is persisted in station.txt;
- [x] AutoSeq sees CQ/POTA change immediately;
- [x] beacon changes via explicit AppAction/controller;
- [x] beacon starts OFF and remains runtime-only;
- [x] beacon parity behavior uses existing TxLifecycle;
- [x] active QSO priority remains unchanged;
- [x] T021 plan produces correct AG6AQ/CM97 CQ text from station.txt;
- [x] T022 physical TX regression remains green;
- [x] Linux CTest passes;
- [x] units pass;
- [x] architecture checks pass;
- [x] real ADV build passes;
- [x] no unrelated changes.

## Hardware/operator acceptance

After supervisor review, the architect only needs a short operator validation:

1. launch the T023 build with QMX as in T022;
2. enter `O -> 4`;
3. set line 1 to `CQ POTA`;
4. set line 2 to desired `EVEN` or `ODD`;
5. return to RX screen;
6. observe automatic on-air CQ in that parity;
7. confirm RxTxLog T line says:
   ```text
   CQ POTA AG6AQ CM97
   ```
8. if a station replies, let existing T022 AutoSeq continue the QSO.

This operator test can also complete the remaining T022 full-QSO acceptance if a
real contact succeeds.

## Automated gate

Run:

```bash
git status --short

cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure

cmake -S tests/unit -B /tmp/T023-build-unit
cmake --build /tmp/T023-build-unit -j"$(nproc)"
ctest --test-dir /tmp/T023-build-unit --output-on-failure

PYTHONDONTWRITEBYTECODE=1 python3 tests/app_dependency_boundary.py . ft8
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_platform_boundary.py . ft8
PYTHONDONTWRITEBYTECODE=1 python3 tests/ft8_platform_boundary.py .
PYTHONDONTWRITEBYTECODE=1 python3 tests/serial_protocol_boundary.py .
PYTHONDONTWRITEBYTECODE=1 python3 tests/architecture_rules.py .

source ~/projects/esp-idf/export.sh
idf.py -C platform/adv build

git diff --check
```

No GitHub Actions wait.

## Branch workflow

Use:

```text
codex/T023-cq-beacon-ui
```

Codex:

1. implement only the two O->4 controls;
2. preserve the T022 physical TX implementation unchanged except for tests that
   exercise the new settings;
3. run all local gates;
4. set Status to REVIEW;
5. record exact files/tests;
6. commit and push one reviewable implementation commit;
7. return commit SHA;
8. no PR;
9. no Actions wait;
10. no RF testing by Codex.

## Codex implementation notes

### Implementation summary

Wired only O -> 4 CQ / Beacon through UI-owned model values and explicit semantic
AppActions. The controller maps these to the existing configuration and beacon
helpers. This branch remains stacked on T022's hardware-tested base
`bb0de152681a85ee4684dcbde5349fdb1146212b`; no rebase onto main.

### Files changed

- `apps/ft8/include/ft8/app_types.h`: CQ/beacon presentation values, model facts,
  and explicit set actions; no configuration/domain headers added to UI.
- `apps/ft8/src/ui_shell/ui_shell.c`: real two-line rendering and directional
  input action generation.
- `apps/ft8/src/app_controller/app_controller.c`: factual model projection,
  CQ validation/synchronization/persistence and runtime beacon action dispatch.
- `tests/ft8_ui_smoke.c`: compact rendering, keyboard cycles, action separation,
  model immutability and unchanged Back navigation.
- `tests/ft8_physical_tx_test.c`: controller persistence/failure cases, beacon
  lifecycle/parity/priority, physical POTA plan and active-TX action freeze.
- This task packet: implementation evidence and REVIEW status.

### UI behavior

O then 4 enters the existing CQ submenu. Number 1 toggles CQ/CQ POTA; number 2
cycles OFF/EVEN/ODD. Enter and Right cycle the selected control forward, Left
cycles backward. All content fits the ADV 20-column layout. Input emits actions
without modifying the supplied model. Back and other navigation remain unchanged.

The existing parser still accepts other historical CQ types. These are not added
to the UI cycle: an out-of-scope loaded type is represented as unavailable (`--`),
without changing the configuration; operating line 1 selects ordinary CQ. This
avoids falsely displaying CQ while a different persisted type remains active.

### Controller/config behavior

CQ actions accept only the two UI values, map explicitly to FT8_CONFIG_CQ/POTA,
call the existing setter, synchronize AutoSeq, and use the existing atomic save.
The resulting station values are `cq_type=0` and `cq_type=2`. A failed save returns
false and restores the prior runtime CQ type/AutoSeq configuration. No new
configuration format/key is introduced.

Beacon actions validate OFF/EVEN/ODD before calling the existing setter. No
beacon value is serialized, initialization remains OFF, and mode changes retain
the existing stale one-shot CQ removal. Existing QSO/free-text priority, parity,
T022 active-TX action freeze, plan immutability, RX lifecycle, CAT behavior and
physical timing are unchanged. No edits to T022 executor, scheduler, radio,
RX implementation, AutoSeq state machine or configuration parser.

### Tests run

```bash
cmake -S . -B build-linux
cmake --build build-linux -j8
ctest --test-dir build-linux --output-on-failure -R 'ft8_ui|ft8_physical_tx'
# Focused UI/physical integration: 2/2 PASS
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure
# Full Linux suite: 54/54 PASS, including T022 physical TX and architecture self-tests

cmake -S tests/unit -B /tmp/T023-build-unit
cmake --build /tmp/T023-build-unit -j8
ctest --test-dir /tmp/T023-build-unit --output-on-failure
# Portable units: 15/15 PASS

PYTHONDONTWRITEBYTECODE=1 python3 tests/app_dependency_boundary.py . ft8
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_platform_boundary.py . ft8
PYTHONDONTWRITEBYTECODE=1 python3 tests/ft8_platform_boundary.py .
PYTHONDONTWRITEBYTECODE=1 python3 tests/serial_protocol_boundary.py .
PYTHONDONTWRITEBYTECODE=1 python3 tests/architecture_rules.py .
# All exit 0

source /home/wei/projects/esp-idf/export.sh
idf.py -C platform/adv build
# Real ESP32-S3 build PASS; minishell_adv.bin 0xbbcd0 bytes, 88% app partition free

git diff --check
# PASS
```

Controller tests load AG6AQ/CM97/cq_type=0 through the normal fixture path, verify
both encoded CQ texts and exact persisted values, inject atomic rename failure,
reject invalid actions, and exercise forward/reverse beacon sequences without FS
writes. Physical integration verifies POTA text before mocked CAT keying on both
parities, skips wrong-parity opportunities, and ignores settings while the plan is
active. Separate reply/free-text cases retain their assigned parity and prevent
beacon CQ from taking their place. All earlier T022 assertions remain enabled.

### Hardware validation still required

After supervisor review, perform the short O -> 4 operator acceptance above with
QMX, verify selected parity and `CQ POTA AG6AQ CM97` in the RT trace, and continue
a QSO if a station replies. No RF or hardware testing was performed by Codex.

### Known limitations / risks

The controls inherit T022's physical-TX and first-QSO hardware acceptance status.
Actions during active TX are ignored rather than queued, as authorized. Historical
CQ types remain file-configurable but are unavailable in this two-choice UI.
No task deviations or unrelated cleanup.

### Commit

One implementation commit on `codex/T023-cq-beacon-ui`, titled
`T023: wire CQ type and beacon UI controls`. Full pushed SHA is returned in the
handoff; this packet is included in that commit. No PR or Actions wait.

## Supervisor review

Supervisor checks the exact diff, especially that T023 does not modify T022
scheduler/radio/RX semantics.

## Architect test result

Record selected CQ type, beacon parity, RT T-line, and any completed QSO.

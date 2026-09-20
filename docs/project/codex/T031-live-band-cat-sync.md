# T031 — Live band change CAT synchronization

Status: TESTING

## Architect intent

While MiniFT8-V3 is running, changing the operating band from the O UIScreen
must update an already-connected QMX instead of changing only MiniFT8's local
band state.

Current operator path:

```text
O -> 3 Band
```

The visible/configured band should change immediately as it does today. If QMX
CAT is connected, MiniFT8 should wait **1.0 second after the most recent band
change** and then CAT-sync only the final selected band. Repeated band presses
inside that interval restart the delay, allowing the operator to skip across
intermediate bands without sending CAT for each one.

Example:

```text
20m -> 17m -> 15m -> 10m
      < presses within 1 s >

QMX receives one final sync for 10m.
```

This is a deliberate MiniFT8-V3 difference from the pinned V2 behavior. V2
deferred band CAT synchronization until STATUS exit / reconnect, partly to avoid
relay chatter on radios such as KH1. T031 is specifically for the current V3 QMX
control path.

## Objective

Add a controller-owned, 1-second debounced runtime band-to-CAT synchronization
path for an already-open QMX connection, while preserving startup sync, TX
safety, AutoSeq state, RX transport ownership, and current UI behavior.

## Current context

Current `main` behavior at task creation:

- `ui_shell` already maps O-screen line 3 (zero-based line 2) to
  `APP_ACTION_SET_BAND`.
- `app_controller_apply_action()` updates `config.band_index` and persists
  `station.txt`.
- `app_controller_start_cat()` calls `radio_control_open_qmx(..., dial_hz)`.
- `radio_control_open_qmx()` opens MiniShell Serial and calls
  `radio_qmx_sync()`.
- `radio_qmx_sync()` sends the known-good QMX receive setup:

```text
MD6;
FR0;
FT0;
FA...........;
```

- There is currently no runtime CAT call from `APP_ACTION_SET_BAND`.
- Therefore the UI/config can show a new band while QMX remains on the startup
  frequency until MiniFT8 is restarted.
- Physical TX uses the same controller/radio state, so T031 must prevent a TX
  from starting on the stale QMX dial frequency while a new-band sync is
  pending.

Current canonical FT8 dial frequencies remain:

```text
80m   3.573 MHz
40m   7.074 MHz
30m  10.136 MHz
20m  14.074 MHz
17m  18.100 MHz
15m  21.074 MHz
10m  28.074 MHz
```

## Source of truth

Repository architecture/workflow:

```text
AGENTS.md
docs/README.md
docs/MiniFT8/README.md
docs/MiniFT8/development.md
docs/MiniFT8/ui.md
docs/MiniFT8/architecture.md
```

Current implementation:

```text
apps/ft8/include/ft8/app_types.h
apps/ft8/src/ui_shell/ui_shell.c
apps/ft8/src/app_controller/app_controller.[ch]
apps/ft8/src/app_controller/app_controller_internal.h
apps/ft8/src/app_controller/app_controller_tx_physical.c
apps/ft8/src/config_service/config_service.[ch]
apps/ft8/src/radio_control/radio_control.[ch]
apps/ft8/src/radio_control/radio_qmx.[ch]
apps/ft8/main/ft8_main.c
tests/ft8_radio_control_test.c
tests/ft8_physical_tx_test.c
tests/ft8_ui_smoke.c
```

Pinned V2 behavior reference:

```text
repository: wcheng95/Mini-FT8
commit:     491e757ae6b1e4cfd2b9a6ba10f48b35643849e0

main/main.cpp
main/radio_control.cpp
main/radio_control_qmx.cpp
```

Relevant V2 fact: `sync_radio_to_current_band()` already established that
band synchronization belongs above the CAT transport and uses the radio-control
frequency/mode sync operation. V2 deliberately deferred manual band changes;
T031 intentionally replaces that user-visible timing for V3/QMX with the
1-second debounce defined here.

## Architectural constraints

- `app_controller` remains the sole owner of FT8-domain runtime sequencing.
- `ui_shell` only emits `APP_ACTION_SET_BAND`; it must not know CAT, QMX,
  timers, Serial, or platform details.
- `config_service` remains the source of canonical band index/name/dial
  frequency facts.
- `radio_control` owns the control abstraction and must expose any reusable
  already-open frequency-sync operation needed by the controller.
- `radio_qmx` remains the owner of QMX CAT command syntax.
- MiniShell Serial remains transport-only.
- No Linux-, ESP-IDF-, ADV-, USB-, tty-, or board-specific calls may enter
  portable MiniFT8 code.
- No public MiniShell API change is allowed.
- Do not add heap allocation.
- Do not add a thread/task solely for this debounce. Use the existing
  controller/main-loop progression and MiniShell monotonic time.
- Startup CAT synchronization behavior must remain unchanged.
- Existing physical TX command timing once TX starts must remain unchanged.
- Existing `O -> 3` visible band-selection behavior must remain immediate.
- Existing persistence format and band indexes must not change.
- Do not clear AutoSeq queues merely because the operator changes band.
- Do not redesign RX buffering or slot framing in this task.

## Required behavior

### 1. Immediate local selection

When `APP_ACTION_SET_BAND` is accepted:

- update `config.band_index` immediately;
- preserve the existing immediate UI/model update;
- preserve the existing config persistence behavior.

### 2. Debounced connected-CAT synchronization

If QMX CAT is already open when the band changes:

- arm a runtime band-sync pending state;
- target the **current** configured band, not a stale copied intermediate band;
- schedule the sync for 1000 ms after the most recent accepted band change;
- each additional accepted band change before expiry restarts the 1000 ms delay;
- before expiry, send no band CAT command;
- at/after expiry, send exactly one normal receive sync for the final selected
  band using the existing QMX sequence;
- on success, clear the pending state.

The operator may leave the O UIScreen during the debounce. The synchronization
still occurs; it is controller state, not UI-screen state.

### 3. No-CAT case

If no CAT stream is open:

- O -> 3 continues to update/save the MiniFT8 band normally;
- no error is produced solely because CAT is absent;
- no useless pending timer needs to survive;
- a later normal `app_controller_start_cat()` must still synchronize the
  then-current configured band exactly once as it does today.

### 4. TX safety

A runtime band change must never allow a physical TX to begin before the final
new-band CAT sync succeeds.

Important slot-boundary case:

```text
band changed late in slot N
1 s debounce crosses into slot N+1
```

If a TX opportunity occurs while the band sync is pending, consume/re-anchor
that slot opportunity without transmitting. After CAT sync succeeds, normal TX
eligibility resumes on a later valid FT8 slot. Do **not** start a catch-up or
late TX in the remainder of the slot whose boundary occurred during the
debounce.

If the controller already has a not-yet-started physical TX boundary/pending
state when a band change is accepted, that stale pending start must not survive
the band change.

An already-active physical TX remains governed by the existing controller
freeze behavior; T031 does not redefine mid-transmission editing.

### 5. CAT synchronization failure

A failed runtime band CAT sync must not be silently ignored while MiniFT8
continues with a mismatched UI/log band and radio frequency.

Use the existing application/controller error model:

- leave TX inhibited;
- report the failure through the existing controller/application error path;
- do not consume any semantic AutoSeq TX completion or retry solely because the
  retune failed;
- do not pretend the new frequency was successfully applied.

Do not invent automatic radio reconnect/re-enumeration in T031.

### 6. Radio-control operation

Add the minimal already-open control operation required to reuse
`radio_qmx_sync()`, for example a neutral operation such as
`radio_control_sync_frequency(...)`.

Requirements:

- it operates only on an already-open RadioControl stream;
- it rejects an active/uncertain TX state rather than sending receive-frequency
  setup through an ongoing TX;
- it reuses the existing QMX sync command builder/order;
- it does not open or close the serial stream;
- it does not duplicate QMX CAT command strings in `app_controller`.

Naming is implementation detail; ownership is not.

### 7. Main-loop progression

Expose one bounded controller progression operation for pending CAT work and
invoke it from `ft8_main` every normal loop iteration, before physical TX
progression.

The main loop may know only that controller progression succeeded/failed; it
must not inspect band indexes, debounce deadlines, QMX state, or CAT command
details.

If MiniShell monotonic time is unavailable while an already-open CAT band
change is requested, do not leave an immortal pending timer. A safe immediate
sync fallback is acceptable.

## Suggested implementation shape

This is guidance, not a requirement to copy names literally.

Controller private state may contain facts equivalent to:

```text
cat_band_sync_pending
cat_band_sync_due_ms
```

Band action:

```text
APP_ACTION_SET_BAND
    -> update/save config
    -> if CAT open:
           pending = true
           due = monotonic_now + 1000 ms
           cancel stale not-yet-started TX pending state
```

Loop:

```text
app_controller_step_location()
app_controller_step_cat()       # new bounded progression
app_controller_step_rx()
app_controller_step_tx()
```

While debounce is pending, TX lifecycle observation must still consume/anchor
slot transitions so clearing the debounce cannot create a late catch-up TX in
the same slot.

At expiry:

```text
config.band_index
    -> config_service_band_dial_hz()
    -> radio_control frequency sync
    -> radio_qmx_sync()
    -> MD6; FR0; FT0; FA...;
```

## Non-goals

- No change to band list, active-band filtering, or canonical frequencies.
- No per-profile frequency editor.
- No KH1, QDX, or generic multi-radio runtime band policy.
- No CAT reconnect/re-enumeration recovery.
- No QMX unplug/replug work.
- No RX waterfall redesign.
- No forced RX audio stop/restart merely because VFO frequency changes.
- No clearing of retained RX display rows as part of this task.
- No AutoSeq queue reset on band change.
- No new UI screen, popup, countdown, or “CAT syncing” indicator.
- No station.txt format change.
- No change to TX offset-source behavior.
- No public MiniShell API/ABI change.

## Acceptance criteria

- [ ] O -> 3 still changes the displayed/configured band immediately.
- [ ] Existing startup CAT sync still sends the selected startup band.
- [ ] With CAT connected, one band change sends no runtime CAT before 1000 ms.
- [ ] At/after 1000 ms, the final selected band is synchronized to QMX.
- [ ] Multiple band changes inside the debounce window restart the timer and
      produce only one final CAT sync.
- [ ] Runtime sync uses the existing `MD6; FR0; FT0; FA...` sequence.
- [ ] Runtime sync does not reopen or close the MiniShell Serial stream.
- [ ] No CAT connection remains a valid receive/simulation configuration.
- [ ] A CAT sync failure is surfaced and cannot be followed by physical TX on
      the stale frequency.
- [ ] A TX opportunity occurring during debounce is skipped/consumed, not sent
      late after the retune.
- [ ] Existing AutoSeq queue contents are preserved across a normal band change.
- [ ] Existing physical TX lifecycle/tone scheduling is unchanged once a TX is
      allowed to start.
- [ ] Linux and ADV composition/build boundaries remain clean.
- [ ] No heap allocation is added.
- [ ] No public MiniShell API change is made.

## Automated tests

Add focused regression coverage.

### Radio-control unit

Extend `ft8_radio_control_unit` to verify an already-open runtime frequency
sync:

- startup/open still sends four setup commands;
- clear transcript;
- runtime sync to another band sends exactly the expected
  `MD6;FR0;FT0;FA...........;`;
- no second open occurs;
- no close occurs;
- no `TX;`, `RX;`, or `TA` appears;
- sync rejects active/uncertain TX state;
- short/error write propagation remains correct.

### Controller debounce test

Add a focused controller test, preferably a dedicated
`ft8_band_cat_unit` using the existing FT8 production-source test bundle, or
equivalent equally-isolated coverage.

Required cases:

1. Start CAT on 20m and clear startup transcript.
2. Apply one band action to 17m.
3. Verify model/config changes immediately.
4. At 999 ms after the action: no runtime CAT writes.
5. At 1000 ms or later: exactly one 17m sync.
6. Change 20m -> 17m -> 15m -> 10m with each action less than 1 s apart:
   verify the deadline restarts and only the final 10m sync is sent.
7. Verify no-CAT band changes do not fail and create no CAT writes.
8. Verify runtime sync failure returns controller/application failure and leaves
   TX unable to start on the stale band.
9. Verify a slot boundary crossed while sync is pending does not start TX and
   cannot become a late same-slot TX immediately after sync succeeds.
10. Verify an already-active TX preserves the pre-existing action-freeze
    behavior; T031 must not retune QMX in the middle of TX.

### Existing regressions

Run at minimum:

```bash
cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"

ctest --test-dir build-linux --output-on-failure -R 'ft8_(radio_control|band_cat|physical_tx|ui_smoke)'

ctest --test-dir build-linux --output-on-failure
```

If the focused test uses a different final CTest name, substitute that exact
name in the focused command and record it in the handoff.

Run architecture checks already included by the normal Linux suite. Also run the
real ADV build required by repository policy for shared portable MiniFT8
changes:

```bash
cd platform/adv
idf.py build
```

## Manual / hardware validation

After supervisor diff review and merge, architect validates with real QMX.

Recommended test:

1. Start QMX on a known band, e.g. 20m.
2. Launch normal MiniFT8 with live QMX RX + CAT.
3. Confirm startup sync still places QMX on 14.074 MHz.
4. Enter O and press 3 once to select 17m.
5. Confirm the UI changes to 17 immediately.
6. Confirm QMX changes to 18.100 MHz about one second later without leaving
   MiniFT8.
7. Rapidly press through several bands in less than one second per press.
8. Confirm intermediate bands are not visibly held by QMX long enough to imply
   one CAT retune per keypress; after the final pause QMX lands on the final
   selected FT8 frequency.
9. Return to RX and confirm normal decoding continues on subsequent clean slots.
10. If convenient, leave an AutoSeq/CQ opportunity armed during a band change
    near a slot boundary and confirm no TX occurs on the old band or as a late
    catch-up in the debounce-crossed slot.

Linux/QMX validation is sufficient for CAT semantics. ADV build is required;
ADV/QMX hardware confirmation is desirable after T030 hardware state permits it.

## Codex implementation notes

### Implementation summary

Implemented controller-owned runtime band synchronization with a 1,000,000-us
monotonic debounce. Band selection and persistence remain immediate. Further
accepted band actions restart the interval; expiry synchronizes the current
configured band using the existing QMX receive sequence on the existing stream.
No-CAT selection arms no timer, and later CAT startup syncs the current band.

The new `app_controller_step_cat()` runs before RX and physical TX progression.
Missing monotonic time at the action or at progression uses immediate sync;
a backward clock also avoids an immortal deadline. `UINT64_MAX` in the private
change timestamp marks the missing-clock fallback.

Runtime sync failure latches the existing application error path (CAT error
return 12), keeps TX inhibited, and does not retry commands or consume AutoSeq
completion/retry state. Startup success and shutdown clear the private state.

The controller cancels a retained, not-yet-started TX boundary on band action.
During debounce, slot observations continue but cannot initiate physical TX.
CAT progression consumes the current slot before and after blocking sync, so a
boundary first noticed at expiry, or crossed during the writes themselves,
cannot become a late same-slot transmission. Later natural slots remain eligible.

`radio_control_sync_frequency()` rejects closed/invalid control and active or
uncertain TX, then delegates to `radio_qmx_sync()` without opening/closing Serial.
The operation remains radio-domain code; no command strings enter the controller.

### Files changed

- `apps/ft8/src/radio_control/radio_control.[ch]`: already-open frequency sync.
- `apps/ft8/src/app_controller/app_controller.[ch]`: arm/reset pending state and
  expose bounded CAT progression.
- `apps/ft8/src/app_controller/app_controller_internal.h`: fixed private pending,
  failure, and timestamp fields; no new allocation calls.
- `apps/ft8/src/app_controller/app_controller_tx_physical.c`: CAT progression,
  slot consumption, and physical-TX inhibition, reusing the existing UTC helper.
- `apps/ft8/main/ft8_main.c`: call CAT progression and use the existing error exit.
- `tests/ft8_radio_control_test.c`: already-open sync, exact transcript, handle
  lifetime, active/uncertain TX rejection, and short/error propagation.
- `tests/ft8_physical_tx_test.c`: isolated `--band-cat` regression using the
  existing production-source fixture and mocked MiniShell services.
- `CMakeLists.txt`: register `ft8_band_cat_unit` for that isolated mode.
- This task packet: implementation and review evidence.

### Invariants preserved

No MiniShell public API, platform provider, UI action/layout, band list/index,
frequency table, persisted format, or CAT command syntax changes. No new task,
thread, or heap allocation. No RX stop/restart, display-row clear, or AutoSeq
queue reset on band selection. Active physical TX still freezes actions; its
existing absolute tone scheduling and completion path are unchanged.

Inspected the pinned V2 `main/main.cpp` band-sync guard/control call and the
existing radio-control/QMX implementation. The one-second V3 timing is the
explicit T031 difference, rather than a copy of V2's deferred manual-band policy.
No architectural or scope deviation.

### Local tests run

```bash
cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure \
  -R 'ft8_(radio_control|band_cat|physical_tx|ui_smoke)'
# PASS 4/4.

PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure
# PASS 64/64; linux_serial_unit passed without retry.

cmake -S tests/unit -B /tmp/T031-build-unit
cmake --build /tmp/T031-build-unit -j"$(nproc)"
ctest --test-dir /tmp/T031-build-unit --output-on-failure
# PASS 15/15.

PYTHONDONTWRITEBYTECODE=1 python3 tests/app_dependency_boundary.py . ft8
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_platform_boundary.py . ft8
PYTHONDONTWRITEBYTECODE=1 python3 tests/ft8_platform_boundary.py .
PYTHONDONTWRITEBYTECODE=1 python3 tests/architecture_rules.py .
# All PASS; also included in the full Linux suite.

source ~/projects/esp-idf/export.sh
idf.py -C platform/adv build
# PASS real ESP32-S3 firmware build.

git diff --check
# PASS.
```

The first focused test run caught a test expectation typo (`17` instead of the
existing model's `17m`). Corrected that assertion without changing UI/model
production behavior; the focused and full suites then passed.

Coverage includes 999-ms suppression/1000-ms sync; rapid 20m/17m/15m/10m selection
and final-only frequency; immediate model and persisted band; no-CAT operation
and later startup; unchanged AutoSeq bytes/RX start-stop counts; short and failed
runtime CAT writes with no automatic retry or TX; slot crossing both with and
without an intervening pending-TX step; blocking writes crossing a boundary;
retained RX-freshness pending start cancellation; later-slot TX resumption;
active-TX freeze; and missing/lost monotonic time fallback.

### Manual/hardware validation still required

After supervisor review/merge, validate the task's Linux/QMX procedure: immediate
O -> 3 display selection, final frequency after approximately one second,
rapid-band coalescing, subsequent clean-slot RX decoding, and no stale/late TX
near a slot boundary. ADV/QMX confirmation is desirable when T030 hardware state
permits it. No flashing or RF test was performed.

### Known limitations / risks

The existing blocking receive-safe sequence may take up to four Serial timeout
budgets; transport acceptance is not a radio frequency readback. A failed or
partial sync deliberately ends normal application progression rather than
inventing reconnection/retry policy. RX buffers and slot framing remain untouched,
as required. Fixed private fields increase the controller object size slightly
without adding any allocation site. Pre-existing untracked Python cache
directories in `platform/adv` and `tests` were left untouched and excluded.

### Commit

One implementation commit on `codex/T031-live-band-cat-sync`, based on task head
`65d06dd`. The commit containing these notes is the implementation reference;
its exact SHA is returned in the Codex handoff. Task set to REVIEW. No PR or
GitHub Actions wait.

## Supervisor review

Reviewed implementation commit:

```text
0791833f00f4adc0d67784ea77000d52da8b8819
```

Result: **PASS — ready for hardware validation.**

Review findings:

- implementation is one clean commit directly ahead of the T031 task head;
- O -> 3 still changes/persists the local band immediately;
- controller owns the 1-second debounce and reads the final current band at sync time;
- runtime CAT reuses the existing QMX `MD6; FR0; FT0; FA...` sequence on the already-open stream;
- no MiniShell public API or platform provider changed;
- active/uncertain TX is rejected by the radio-control sync operation;
- band edits cancel stale not-yet-started TX state;
- slot progression is consumed while debounce is pending and again after blocking CAT writes, preventing old-frequency or late same-slot key-up;
- CAT failure latches the application error path without AutoSeq completion/retry consumption;
- no-CAT startup behavior remains valid and later CAT startup uses the current band;
- focused tests cover 999/1000 ms timing, rapid-band coalescing, CAT failures, slot-boundary crossing, active-TX freeze, missing monotonic time, and later-slot TX resumption;
- reported Linux 64/64, portable units 15/15, architecture checks, ADV build, and diff check are sufficient software evidence for merge.

No blocking review findings. Canonical current-state documentation should be updated after real QMX acceptance.

Remaining gate: architect QMX hardware validation described above.

## Architect test result

Pending.

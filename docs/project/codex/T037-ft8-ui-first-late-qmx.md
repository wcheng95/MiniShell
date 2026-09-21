# T037 — Restore UI-first ADV FT8 startup with late QMX attach

Status: READY

## Architect intent

Restore the original accepted ADV behavior that was regressed by T030 R1:

```text
M$> ft8
    -> normal MiniFT8 UI appears immediately
    -> user can navigate/read/quit with no QMX attached
    -> QMX may be attached later
    -> CAT synchronization then succeeds
    -> live RX starts
```

The current pre-application screen:

```text
ft8: waiting for QMX
Q/Esc: cancel
```

is not the desired product behavior.

T030 R1 commit `51e45d631d285fb7080c2bee643b1a90a292617e`
correctly preserved indefinite first attachment, but implemented the wait outside
the application before `adv_ft8_entry()`. T037 keeps the late-attach transport
fix while restoring **UI first, hardware later**.

## Baseline

```text
main = 1240d56226dd9792d1d82629315df79eb6fcffdf
```

T036 is BREAK/reverted. Do not reintroduce its web mirror.

Accepted invariants:

- portable MiniFT8 architecture remains platform-independent;
- ADV packaged profile remains time_osr=2 / freq_osr=1;
- CPU1 USB Host lifetime/interrupt ownership remains unchanged;
- QMX UAC-IN + CDC share the same USB session;
- CAT synchronization must be truthful; unsent commands never count as success;
- receive-safe QMX CAT synchronization precedes actual live RX use;
- WebFS T033-T035 remains unchanged.

## Root cause

Current `platform/adv/main/ft8_static.c` does:

```text
adv_qmx_prepare_serial()
    loop until CDC/QMX ready
        show "waiting for QMX"
        consume only Q/Esc
then
adv_ft8_entry()
```

Therefore the real MiniFT8 UI cannot exist before QMX attachment.

Simply deleting that loop is insufficient because the current ADV
`serial:qmx` open waits up to approximately three seconds and then tears down
an unowned discovery session. Retrying that call from the UI loop would cause
multi-second UI stalls and repeated USB-session churn.

T037 must fix both pieces coherently.

## Required behavior

### 1. MiniFT8 UI is initialized/rendered before live hardware readiness

For normal live operation with configured CAT/RX endpoints:

1. parse/config/storage as today;
2. initialize the presentation adapter;
3. initialize `UiShell`;
4. render the normal MiniFT8 frame;
5. enter the normal application loop;
6. treat CAT `MINI_ERR_NOT_READY` as a pending device rather than startup failure;
7. retry without blocking the UI;
8. after CAT succeeds, start RX and the decode worker exactly once.

While pending:

- all normal read-only/navigation UI remains usable;
- R/T/O/S/V switching works;
- V->1 memory, V->3 QSO and other available screens work;
- Q/Esc quits normally;
- no RX/TX action is fabricated;
- `rx_active` remains false until RX actually starts;
- do not show the old ADV-only "waiting for QMX" screen.

No new special waiting screen is required. The normal UI itself is the waiting UI.

### 2. Retry semantics are portable and generic

In portable `apps/ft8/main/ft8_main.c`:

- `app_controller_start_cat(...) == MINI_ERR_NOT_READY` means "live control
  endpoint not ready yet";
- remain in the normal UI loop and retry later;
- hard results such as IO/UNSUPPORTED/ACCESS/etc. retain truthful error behavior;
- once CAT succeeds, do not reopen/re-sync repeatedly;
- then start the configured RX endpoint and decode worker once;
- if RX startup fails after CAT readiness, preserve the existing result/error path
  rather than hiding it.

Do not teach portable MiniFT8 about QMX, USB, ADV, Cardputer or ESP-IDF.

A small retry interval around 250-500 ms is appropriate. Use monotonic time rather
than blocking sleeps where practical. Do not busy-loop.

Fixture/offline paths must preserve existing behavior:

- deterministic `--rx-slot` tests;
- explicit file/WAV RX;
- CAT tone test;
- Linux/default operation.

Only live control endpoint `NOT_READY` needs the pending lifecycle.

### 3. ADV QMX discovery hold

Replace the current pre-entry wait loop with a private ADV **discovery hold**.

Desired composition:

```text
minishell_app_ft8_main
    compose uac:qmx + serial:qmx defaults
    begin private QMX discovery hold
    call adv_ft8_entry immediately
    end discovery hold on every return path
```

The discovery hold:

- starts/retains the existing shared USB Host/UAC/CDC discovery session;
- does not require a QMX to be attached;
- does not open a public Serial handle;
- has no display/input policy;
- adds no new task;
- is always paired with release on wrapper exit.

Use ADV-private names such as:

```c
mini_result_t adv_qmx_discovery_begin(void);
mini_result_t adv_qmx_discovery_end(void);
```

Exact names are flexible.

### 4. Nonblocking Serial open while discovery is held

When the FT8 discovery hold is active:

`serial_open("serial:qmx")` must:

- check CDC readiness quickly;
- return `MINI_OK` and reserve the normal handle when ready;
- return `MINI_ERR_NOT_READY` promptly when not ready;
- **not** tear down the held session on NOT_READY;
- not spin/wait for three seconds;
- not fabricate a handle.

When the private discovery hold is **not** active, preserve the existing generic
ADV Serial-open behavior, including its bounded approximately three-second
readiness wait and cleanup semantics.

This keeps T037 scoped to FT8 composition instead of silently changing generic
MiniShell Serial behavior.

### 5. Late attachment sequence

With no QMX at FT8 launch:

```text
UI active
  |
  +-- periodic CAT open -> NOT_READY
  +-- user navigation/input continues
  |
QMX attached
  |
USB workers enumerate CDC + UAC
  |
next retry
  |
serial open succeeds
  |
existing radio_qmx_sync:
    MD6;
    FR0;
    FT0;
    FA...........;
  |
RX open/start
  |
decode worker starts
  |
normal live FT8
```

CAT synchronization must still precede live RX activation.

## Scope / non-goals

Do not:

- change FT8 DSP profile/workspace;
- change QMX FIFO 91/18/91;
- change CPU1 USB Host ownership;
- change UAC ring size/buffering;
- add a second USB session;
- add QMX knowledge to portable FT8;
- add public API fields;
- add background app threads merely for startup;
- add reconnect-after-runtime-disconnect policy;
- modify WebFS;
- revisit T036 web UI;
- redesign TX.

This task is specifically startup with **QMX absent initially**.

## Expected code areas

Likely:

```text
apps/ft8/main/ft8_main.c
    UI-first initialization
    pending CAT state
    bounded monotonic retry
    start RX/decode once CAT becomes ready

platform/adv/main/ft8_static.c
    remove blocking pre-entry waiting UI
    private discovery begin/end around adv_ft8_entry

platform/adv/adv_audio_uac.cpp
    private discovery-hold state
    held-session-aware nonblocking serial:qmx open

platform/adv/adv_internal.h
    private discovery begin/end declarations

tests/...
    portable UI-first/pending lifecycle
    ADV late-attach/session-hold regression
```

Keep the implementation smaller if a cleaner equivalent exists.

## Important lifecycle details

### Initial frame

The normal MiniFT8 frame must be rendered before a missing live CAT endpoint can
block or fail startup.

A test should prove display/render activity occurs before the first pending
Serial-open retry result is allowed to terminate anything.

### Input while pending

The normal MiniFT8 input path remains active.

At minimum prove while CAT returns repeated NOT_READY:

- V then 3 enters QSO/log view;
- O/S/R navigation remains possible;
- Q or Escape exits cleanly.

Do not create a reduced "waiting input" loop.

### RX/decode startup

RX and decode worker must start once only, after CAT success.

If QMX is already attached at launch, startup should remain fast and behaviorally
equivalent to the current accepted path.

### Cleanup

If the user quits while QMX is absent:

- no Serial handle exists;
- no Audio handle exists;
- discovery hold is released;
- USB Host/classes unwind through the existing ownership code;
- resident console ownership returns correctly;
- subsequent `usbmsc` and `ft8` remain usable.

If portable initialization fails after discovery begin, wrapper cleanup must still
release the hold.

## Tests

Add a portable lifecycle regression using real `ft8_main.c` logic and fakes:

### QMX/control absent initially

- normal UI renders before control endpoint readiness;
- Serial open returns NOT_READY repeatedly;
- app does not exit;
- input remains processed between retries;
- V->3/read-only navigation can occur;
- Q/Esc exits successfully;
- RX open and decode worker never start before CAT success.

### Late readiness

- return NOT_READY for several retry periods;
- then make control endpoint ready;
- existing CAT synchronization runs exactly once;
- RX open/start occurs only after CAT sync;
- decode worker starts exactly once;
- main loop continues normally.

### Hard CAT failure

- a hard error other than NOT_READY still produces the existing CAT startup
  failure/result after the UI has rendered;
- do not loop forever on IO/UNSUPPORTED.

### Already connected

- immediately ready CAT follows the same sync-before-RX ordering;
- no unnecessary retry delay.

### ADV provider/composition

Use the production ADV Serial/session functions with deterministic mocks to prove:

- discovery begin starts one session without requiring CDC readiness;
- held `serial_open` returns NOT_READY promptly and does not release/restart session;
- many pending retries keep exactly one discovery session;
- attachment later makes the next open succeed;
- normal QMX command bytes are transmitted once;
- RX can then reserve the same session;
- discovery end releases the session after app owners close;
- quitting before attachment releases cleanly;
- outside a discovery hold, existing ~3 s Serial-open behavior remains preserved.

Update/remove the old regression assertions that require:

```text
"ft8: waiting for QMX"
entry == 0 before attach
```

Those assertions encode the regression T037 is fixing.

Run all existing gates:

```bash
cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure

cmake -S tests/unit -B /tmp/T037-build-unit
cmake --build /tmp/T037-build-unit -j"$(nproc)"
ctest --test-dir /tmp/T037-build-unit --output-on-failure

PYTHONDONTWRITEBYTECODE=1 python3 tests/architecture_rules.py .
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_dependency_boundary.py . ft8
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_platform_boundary.py . ft8
PYTHONDONTWRITEBYTECODE=1 python3 tests/ft8_platform_boundary.py .

source ~/projects/esp-idf/export.sh
idf.py -C platform/adv build

git diff --check
```

Also rerun focused QMX/USB/UAC/ADV FT8 regressions and all T032 QSO-view tests.

## Memory / resource constraints

This is a lifecycle correction and should add essentially no meaningful runtime
memory.

Record:

- firmware delta;
- .dram0.data/.bss/.iram0.text delta;
- any new static state;
- task/stack changes (none expected);
- heap allocations (none expected).

Do not accept a large memory increase for this fix.

## Hardware acceptance

After supervisor review:

### H1 — no QMX at launch

1. Boot ADV with QMX disconnected.
2. Run `ft8`.
3. Normal MiniFT8 UI must appear promptly; no "waiting for QMX" replacement screen.
4. Navigate R/T/O/S/V.
5. Enter V->3 and other read-only pages.
6. Wait at least 10 seconds.
7. UI must remain responsive.
8. Q/Esc must quit normally.

### H2 — late attach

1. Start FT8 with QMX disconnected.
2. Navigate UI for at least 5 seconds.
3. Attach QMX.
4. Verify CDC/UAC enumerate.
5. Verify CAT sync occurs.
6. Verify RX becomes active without restarting FT8.
7. Verify live decoding across consecutive slots.

### H3 — already connected

Start FT8 with QMX already attached and verify accepted startup/decode behavior is
unchanged.

### H4 — cleanup/retry

```text
ft8(no QMX) -> navigate -> quit
usbmsc -> quit
ft8(no QMX) -> attach QMX -> RX
quit
ft8(QMX connected) -> RX
```

All must work in one boot.

## Codex implementation notes

Codex fills this section before handoff.

### Implementation summary

### Files changed

### Invariants preserved

### Memory / firmware evidence

### Local tests run

### Manual/hardware validation still required

### Known limitations / risks

### Commit

## Supervisor review

Review the exact implementation diff. Special attention:

- normal UI genuinely renders before hardware readiness;
- pending path remains fully interactive;
- no three-second Serial-open stalls while FT8 discovery hold is active;
- one retained discovery session, no churn/leak;
- CAT sync still precedes RX;
- hard errors remain errors;
- Q/Esc cleanup with no QMX;
- no portable QMX/ADV knowledge;
- no USB ownership/DSP/WebFS changes.

No PR required.

## Architect test result

Pending.

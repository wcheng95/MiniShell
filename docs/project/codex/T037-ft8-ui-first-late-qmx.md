# T037 — Restore UI-first ADV FT8 startup with late QMX attach

Status: TESTING

Architect clarification: **Escape remains Back; Q quits**, including while CAT
is pending. This overrides the Q/Esc exit wording below.

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

### Implementation summary

Implemented from `29c445793e5bbd1d7fa915e6089f71f331b5a591` on
`codex/T037-ft8-ui-first-late-qmx`.

**Architect clarification during implementation:** preserve Escape as Back; Q
quits. This supersedes the packet's Q/Esc exit wording. No pending-only input
policy or new waiting screen was introduced.

Portable MiniFT8 initializes UiShell and presents its normal frame before the
first CAT open. Live CAT `MINI_ERR_NOT_READY` remains pending in the normal loop,
with a 300 ms monotonic retry interval. Normal navigation, QSO-view actions,
location/model updates and Q remain available. Escape retains normal submenu/back
navigation. Hard CAT errors still return 12 after the initial UI; RX startup
failure returns 8 and decode-worker failure returns 14. After CAT succeeds, RX
and the decode worker start once, in that order, with no further startup retries.
RX remains inactive and TX progression is gated until startup is complete.

ADV composition now begins a private discovery hold, invokes the portable entry
without waiting for a device, and ends the hold after entry returns. The hold
retains the existing shared session across pending Serial opens and public-handle
closes. Held Serial opens use a zero-time CDC mutex check and return NOT_READY
without reserving a handle, delaying, releasing or restarting discovery. A stopped
CDC worker is a hard IO result. Outside the hold, the existing approximately
three-second open/readiness wait and cleanup path are unchanged.

The wrapper no longer renders `ft8: waiting for QMX` or consumes input before the
portable application. Begin/end failures retain the existing result 12 and
cleanup-error reporting. Tone-test and malformed/explicit endpoint composition
paths retain their prior dispatch.

### Files changed

- `apps/ft8/main/ft8_main.c`: initial normal frame, generic pending CAT lifecycle,
  monotonic retries and one-time RX/worker activation.
- `platform/adv/main/ft8_static.c`: replace pre-entry waiting/input with begin/end.
- `platform/adv/adv_audio_uac.cpp`: private discovery ownership and held-open check.
- `platform/adv/adv_internal.h`: private discovery begin/end declarations.
- `tests/ft8_pending_start_test.c`: actual main/UI adapter/UiShell/CAT synchronizer
  with fake controller services and deterministic time/readiness.
- `tests/adv_qmx_serial_test.py`: real provider/composition hold and late-attach
  coverage; replace stale assertions requiring no portable entry before attach.
- `tests/adv_ft8_defaults_test.py`: new private composition-hook names.
- `tests/adv_uac_allocation_test.py`: declare the new private hold state in the
  existing extracted-provider harness; all allocation/failure assertions retained.
- `CMakeLists.txt`: register the portable pending-start lifecycle regression.
- This task packet: clarification, implementation evidence and REVIEW status.

### Invariants preserved

No platform/QMX knowledge added to portable lifecycle logic; no public API change.
No changes to FT8 DSP profile/workspace, controller/TX architecture, UAC ring,
FIFO 91/18/91, capture/decode tasks or CPU1 Host install/event/uninstall ownership.
The USB owner, prepare/release implementation and class workers remain unchanged.
One foreground-only bool expresses the discovery hold; no second USB session or
new task. WebFS T033-T035 and Wi-Fi configuration are unchanged. T036 remains
reverted and no web mirror is reintroduced.

Already-ready CAT synchronizes immediately after the initial frame, before RX;
no artificial initial retry delay. CAT tone tests bypass the UI/startup loop as
before. Explicit `--rx-slot` remains deterministic and treats CAT startup errors
as errors; normal no-CAT file/WAV and offline paths start without pending CAT.
No runtime disconnect/reconnect policy was added.

### Memory / firmware evidence

Built the exact baseline before edits and saved its ELF/BIN. Compared the final
real ADV build using the installed ESP-IDF v5.5.4 toolchain,
`xtensa-esp32s3-elf-size -A`, `xtensa-esp32s3-elf-nm -S`, and `wc -c`.

| Measurement (bytes) | Baseline | T037 | Delta |
| --- | ---: | ---: | ---: |
| `.iram0.text` | 63,959 | 63,959 | 0 |
| `.dram0.data` | 27,000 | 27,000 | 0 |
| `.dram0.bss` | 38,864 | 38,864 | 0 |
| Firmware BIN | 1,375,552 | 1,375,776 | +224 |

Total linked static internal-SRAM delta: **0 bytes**. DRAM heap-start remains
`1070219088`; IRAM vectors/end padding and RTC sections are unchanged. The new
`discovery_held` symbol is **1 byte**, absorbed by existing linker padding.
Portable startup adds three local bools and one uint64 monotonic timestamp; it
adds no heap allocation. Normal application resources are now allocated while
hardware is pending, as required for the full UI.

Final firmware is `0x14fe20`; app partition free space is `0x4a01e0` (78%). No task
or configured stack-size changes: foreground remains 16 KiB on CPU0 and all
existing USB/capture/decode task configuration remains unchanged.

### Local tests run

All final gates passed:

```bash
cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure
# 74/74 passed on the final complete run

cmake -S tests/unit -B /tmp/T037-build-unit
cmake --build /tmp/T037-build-unit -j"$(nproc)"
ctest --test-dir /tmp/T037-build-unit --output-on-failure
# 15/15 passed

PYTHONDONTWRITEBYTECODE=1 python3 tests/architecture_rules.py .
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_dependency_boundary.py . ft8
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_platform_boundary.py . ft8
PYTHONDONTWRITEBYTECODE=1 python3 tests/ft8_platform_boundary.py .
# all passed

PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure \
  -R 'adv_(qmx|usb|uac|ft8)|ft8_(pending|qso|ui|band)'
# 11/11 passed, including T032 UI/QSO and QMX/USB/UAC regressions

source ~/projects/esp-idf/export.sh
idf.py -C platform/adv build
# real ADV build passed

git diff --check
# passed
```

The new portable regression runs real `ft8_main.c`, `ft8_ui_adapter.c`, UiShell and
radio synchronization code. It proves an actual Display present precedes Serial
open; 12 simulated seconds of absent hardware with input processed between
300 ms retries; V->3 QSO rendering and O/S/R/T navigation; Escape Back followed by
Q quit; no RX/worker/TX progression while pending; late CAT success after four
NOT_READY results; exactly one `MD6;FR0;FT0;FA00007074000;` sequence before one
RX/worker start; already-connected startup with zero initial delay; immediate and
late hard CAT errors; RX/worker/render failure results; explicit slot/file/offline
and tone-test paths.

The ADV provider/composition regression now proves 200 prompt held NOT_READY
opens without clock advancement or session churn, zero-wait mutex contention,
attachment after the old three-second deadline, same-session CAT/UAC ownership,
public-handle closes retaining the hold, wrapper release after normal/cancel/
initialization/error returns, no waiting-screen output, and unchanged generic
three-second timeout/cleanup behavior. Existing USB-owner and allocation tests
remain intact.

Two earlier complete runs hit the unchanged `linux_serial_unit` PTY assertion at
`tests/linux_serial_test.c:67` (assuming no further write capacity after its
initial queue-fill loop). An isolated rerun and complete runs passed, including
the final 74/74 run. Linux Serial provider/test sources were not changed or
weakened; this intermittent host PTY result is recorded rather than hidden.

### Manual/hardware validation still required

No hardware testing or flashing performed. After supervisor review, run H1-H4:
no-QMX startup and full navigation for at least ten seconds; Q exit; late attach
while UI is active; CAT synchronization then continuous RX/decode; already-
connected behavior; and same-boot `ft8(no QMX) -> Q -> usbmsc -> quit -> ft8 ->
attach -> RX -> Q -> ft8`. Use Escape as Back, per architect clarification.
Actual startup latency, UI responsiveness during physical enumeration, console
restoration and continuous live decoding remain hardware acceptance items.

### Known limitations / risks

- This is first-attachment startup behavior only; runtime unplug/reconnect policy
  is unchanged.
- Pending retries require MiniShell's monotonic clock. A NOT_READY result in
  explicit fixture mode or without a monotonic service remains a startup error,
  avoiding an unbounded busy retry in unsupported service compositions.
- Once CDC is available, existing bounded CAT synchronization/write timeout
  behavior remains; the change removes the absent-device Serial-open stall.
- Hard USB discovery initialization/cleanup failures still return result 12;
  readiness is not fabricated. Incomplete teardown retains existing retryable
  session ownership.
- Host PTY timing-test intermittency is described above; all final gates passed.

### Commit

One implementation commit on `codex/T037-ft8-ui-first-late-qmx`, parent
`29c445793e5bbd1d7fa915e6089f71f331b5a591`, titled
`Restore UI-first FT8 startup with held late QMX discovery`.
Exact pushed SHA is returned in the handoff. No PR.

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

## Supervisor review — implementation

Reviewed implementation commit:

```text
79b8373521295363c628d69db249360ce57ee7f0
```

Result: **PASS — ready for ADV hardware validation.**

The implementation restores the intended UI-first lifecycle without undoing T030 transport ownership:

- normal MiniFT8 frame is rendered before the first live CAT readiness attempt;
- `MINI_ERR_NOT_READY` from a live CAT endpoint becomes a 300 ms monotonic pending retry;
- the pending path remains in the normal MiniFT8 loop with ordinary R/T/O/S/V navigation;
- Escape retains normal Back behavior and Q quits;
- RX remains inactive and TX progression is gated while CAT is pending;
- CAT synchronization runs once when readiness appears;
- RX and the decode worker start once, only after CAT success;
- hard CAT errors remain hard errors;
- fixture/file/offline/tone-test paths preserve their existing semantics;
- portable MiniFT8 contains no QMX/ADV/USB knowledge;
- the ADV wrapper no longer renders or owns a `waiting for QMX` UI;
- ADV composition begins one private QMX discovery hold before entering MiniFT8 and ends it on return;
- held `serial:qmx` opens use a zero-time CDC readiness check and return NOT_READY promptly without session churn;
- outside the discovery hold, the existing approximately three-second generic Serial-open behavior is unchanged;
- CAT and UAC public handles share the same retained USB session;
- final discovery-end cleanup uses the existing `release_unused()` ownership path;
- CPU1 USB Host ownership, FIFO 91/18/91, UAC buffering, FT8 DSP profile, WebFS and public APIs are unchanged.

Validation evidence is sufficient for hardware testing: Linux 74/74, portable 15/15, focused 11/11, architecture checks, real ADV build and diff check all pass.

Resource impact:

- firmware: +224 B;
- permanent static SRAM: 0 B;
- no new heap allocation;
- no task or stack-size change.

Hardware acceptance should now focus on no-QMX UI responsiveness, late physical attachment, already-connected startup, and same-boot cleanup/retry.
## Architect test result

Pending.

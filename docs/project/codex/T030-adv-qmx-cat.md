# T030 — ADV QMX CAT + shared UAC/CDC USB session + first ADV QSO

Status: TESTING

## Architect intent

Bring the proven Linux/QMX CAT + physical FT8 TX boundary to Cardputer ADV so
ADV can make its first real MiniFT8-V3 QSO.

**Portable MiniFT8 must not change in T030.** The existing MiniFT8 Serial/CAT/TX
code is already the accepted implementation. T030 is an ADV MiniShell platform
transport/composition task.

The portable MiniFT8 CAT/TX stack is already complete and hardware-proven on
Linux. Do not redesign it.

The missing piece is a thin ADV service adaptation around the already-proven V2-style transport:

- QMX UAC RX already works on ADV;
- the ADV UAC provider already installs `usb_host_cdc_acm`, opens QMX
  VID:PID `0483:a34c`, interface 0, and keeps a private CDC handle;
- that CDC handle is currently lifecycle scaffolding only and explicitly sends
  no CAT commands;
- ADV currently exposes no public MiniShell Serial service.

T030 must expose the already-open QMX CDC path through MiniShell Serial and keep
the existing V2-style UAC+CDC USB session alive across TX.

## Primary result

Bare ADV operation should become:

```text
M$> ft8

ADV wrapper:
    RX  = uac:qmx
    CAT = serial:qmx

QMX composite USB device
    |
    +-- UAC IN -> MiniShell Audio RX -> MiniFT8 RX
    |
    `-- CDC interface 0 -> MiniShell Serial -> radio_qmx CAT
                                   |
                                   +-- MD6 / FR0 / FT0 / FA...
                                   +-- TX
                                   +-- TA tone updates
                                   `-- RX

No QMX/QDX UAC OUT path is part of T030.
```

The final hardware acceptance goal is a completed two-way FT8 QSO using:

```text
Cardputer ADV + QMX
```

with live RX, CAT-keyed 79-symbol TX, RX recovery, and normal MiniFT8 logging.

## Primary hardware reference — MiniFT8-V2

Before designing anything new, use the pinned V2 implementation as the
hardware/lifecycle reference:

```text
wcheng95/Mini-FT8
491e757ae6b1e4cfd2b9a6ba10f48b35643849e0

main/stream_uac.cpp
main/stream_uac.h
main/radio_control_qmx.cpp
main/radio_control.cpp
main/main.cpp
```

V2 has run QMX UAC + CDC CAT on Cardputer ADV hardware for a long time.

Important V2 facts already verified by supervisor:

```text
QMX VID:PID                 0483:a34c
CAT CDC interface           0
CDC driver                  espressif/usb_host_cdc_acm 2.2.0
CAT send                    cdc_acm_host_data_tx_blocking()
USB FIFO                    RX=91 NPTX=18 PTX=91
CDC driver stack            3072
CDC driver priority         4
CAT RX callback             disabled
```

V2 exposes only two tiny CAT transport helpers:

```c
bool cat_cdc_ready(void);
esp_err_t cat_cdc_send(const uint8_t *data, size_t len, uint32_t timeout_ms);
```

and QMX CAT is simply:

```text
MD6;
FR0;
FT0;
FA...........;
TX;
TAxxxx.xx;
RX;
```

During normal QMX FT8 TX, V2 does **not** stop or tear down the UAC host/session.
It keeps UAC + CDC alive concurrently and only mutes RX presentation while CAT
TX proceeds. Full UAC/CDC/USB-host teardown happens only when leaving the USB
audio mode (for example MSC/radio-source change), not once per FT8 transmit slot.

Current V3 ADV already carries most of this exact proven V2 recipe in
`adv_audio_uac.cpp`: same VID/PID, interface 0, CDC component, FIFO partition,
CDC stack/priority, and persistent private CDC handle. T030 should therefore be
a small adaptation of proven V2 behavior into MiniShell services, not a USB
architecture redesign.

## Audio direction scope

MiniShell already separates Audio RX and Audio TX. T030 uses only:

```text
MiniShell Audio RX
    <- QMX UAC IN
```

and separately:

```text
MiniShell Serial
    -> QMX CDC CAT
```

T030 must **not** add or modify a USB Audio OUT provider.

The existing ADV local speaker provider is unrelated and must remain untouched.

Future QDX work may add:

```text
QDX UAC IN  -> MiniShell Audio RX
QDX UAC OUT <- MiniShell Audio TX
QDX CDC     -> MiniShell Serial
```

but that is explicitly deferred.

## Existing portable source of truth

Do not change semantics in:

```text
include/minishell/api.h
docs/api/serial-api.md

apps/ft8/src/radio_control/radio_control.[ch]
apps/ft8/src/radio_control/radio_qmx.[ch]
apps/ft8/src/app_controller/app_controller_tx_physical.c
apps/ft8/src/app_controller/app_tx_schedule.[ch]
apps/ft8/src/tx_encoder/
apps/ft8/src/auto_seq/
```

Linux reference provider:

```text
platform/linux/linux_serial.c
```

Linux/QMX behavior through T019-T029 is the semantic reference.

## Existing ADV source of truth

Read before editing:

```text
platform/adv/adv_audio_uac.cpp
platform/adv/adv_backend.c
platform/adv/adv_internal.h
platform/adv/adv_console.c
platform/adv/main/ft8_static.c
platform/adv/main/CMakeLists.txt
platform/adv/main/idf_component.yml
platform/adv/sdkconfig.defaults
platform/adv/README.md
```

Pinned managed components:

```text
espressif/usb_host_uac      1.3.3
espressif/usb_host_cdc_acm  2.2.0
```

Do not upgrade either component in this task.

The pinned CDC driver supports `cdc_acm_host_data_tx_blocking()`; use the pinned
component API rather than introducing a new USB stack.

## Critical current lifecycle problem

Today `adv_audio_uac.cpp` owns all of these together:

```text
USB Host library
UAC class driver
CDC-ACM class driver
UAC capture task
CDC discovery/reconnect task
QMX UAC handle
QMX CDC handle
console USB-host handoff
```

and current `Audio.stop()` calls the full `release()`, which tears down:

```text
UAC
CDC
USB Host
```

That is incompatible with physical TX.

Portable MiniFT8 intentionally performs:

```text
pause RX Audio
    -> CAT MD6; TX;
    -> 79 x CAT TA....
    -> CAT RX;
resume RX Audio
```

Therefore ADV must preserve the CDC/Serial connection while UAC streaming is
stopped.

This is the architectural center of T030.

## Required ownership model — minimal V2-style adaptation

Prefer the smallest change to the existing `adv_audio_uac.cpp`.

A new `adv_qmx_usb` abstraction is **not required**. Do not refactor merely for
architectural neatness.

The current ADV UAC-IN provider already owns the correct shared QMX USB host,
UAC RX driver, CDC driver, capture task, CDC task, and console handoff. Extend
that existing owner so MiniShell Audio RX and Serial can share it safely.

Minimum state distinction needed:

```text
QMX USB session alive
Audio-RX public owner/open
Audio-RX logical started/stopped
Serial public owner/open
CDC handle ready/disconnected
```

Rules:

1. one USB Host installation only;
2. one QMX CDC interface-0 handle only;
3. Serial may cause the QMX USB session to come up before Audio, because portable
   `ft8_main.c` opens CAT before RX;
4. Audio.open joins an already-live session rather than tearing it down/reopening;
5. Audio.stop/start must not destroy CDC/USB Host;
6. Audio.close releases the Audio owner;
7. Serial.close releases the Serial owner;
8. full UAC/CDC/USB-host teardown + console restoration occurs only when no
   public owner remains;
9. preserve the already-validated V2/ADV disconnect cleanup behavior;
10. no second USB stack and no duplicate class-driver instance.

## Portable MiniFT8 must remain unchanged

No files under:

```text
apps/ft8/
```

may be changed for production behavior in T030.

In particular, do not modify:

```text
apps/ft8/main/ft8_main.c
apps/ft8/src/app_controller/
apps/ft8/src/radio_control/
apps/ft8/src/tx_encoder/
apps/ft8/src/auto_seq/
```

Tests may of course exercise those existing interfaces.

The existing MiniFT8 behavior is already correct:

```text
CAT open
RX open/start
...
before TX -> Audio RX stop
CAT TX/TA/RX
after TX  -> Audio RX start
```

ADV must adapt to that contract.

## ADV Serial provider

Expose a MiniShell Serial provider on ADV.

Endpoint:

```text
serial:qmx
```

This is platform-specific endpoint syntax and remains below the public API
semantics.

For T030, **WRITE capability is sufficient and preferred**:

```text
MINI_SERIAL_CAP_WRITE
```

MiniFT8 `radio_control_open_qmx()` only requires WRITE.

Do not add receive parsing or a general USB serial terminal unless needed for
correct driver operation.

Required Serial behavior:

### open

```text
serial:qmx
    -> acquire shared QMX USB session
    -> install/start CDC driver if needed
    -> open 0483:a34c interface 0
    -> wait boundedly for CDC ready
    -> return one backend Serial handle
```

Invalid endpoints must return a normal MiniShell error.

A second public Serial open remains rejected by the resident Serial service.

### write

Use the pinned CDC driver's blocking TX API.

MiniShell semantics remain:

- finite timeout bounds transport progress;
- success reports the number of bytes accepted;
- error reports count 0;
- no CAT framing belongs in the provider.

CAT strings remain owned by `radio_qmx.c`.

All current QMX commands are small, including:

```text
MD6;
FR0;
FT0;
FA...........;
TX;
TA1500.00;
RX;
```

### close

Close/release the CDC Serial owner.

If Audio still owns the QMX session, UAC + Host remain alive.

If Serial was the last owner, perform complete class/host teardown and restore
normal ADV USB console ownership.

## UAC-IN / Audio-RX lifecycle change — preserve V2 concurrent USB behavior

Keep RX endpoint:

```text
uac:qmx
```

The important distinction is **MiniShell logical Audio state** versus the
underlying shared QMX USB session.

### Audio.open

- acquire/join the existing QMX USB session;
- allocate/prepare the normal RX ring and capture infrastructure as needed;
- do not disturb an already-open CDC Serial owner.

### Audio.start

- enable delivery of fresh QMX samples to MiniShell;
- on first start, begin the existing UAC stream normally;
- after a TX pause, discard stale samples and resume with a discontinuity/reset
  boundary as needed.

### Audio.stop

Do **not** call the current full `release()`.

Match V2's proven hardware strategy: keep the QMX USB Host, UAC device, and CDC
device alive across an FT8 transmit slot.

MiniShell's logical RX must be stopped, meaning the application receives no
old/queued RX samples while paused. The provider may keep the physical UAC
stream running internally and drain/discard samples during the pause. This is
preferred if it keeps the implementation small and avoids stop/start races.

Required result:

```text
Audio.stop()
    -> MiniShell RX delivery paused / stale ring discarded
    -> underlying QMX UAC may remain streaming and drained
    -> CDC handle remains valid
    -> CAT TX immediately usable
```

This is intentionally analogous to V2, where UAC + CDC remain live throughout
QMX CAT TX.

### Audio.start after TX

- re-enable delivery from the still-live UAC session;
- ensure no TX-slot/stale samples are handed to the decoder;
- emit/preserve the existing discontinuity/reset behavior so MiniFT8
  re-establishes slot timing.

### Audio.close

Release Audio ownership and RX resources. Only perform complete shared QMX
session teardown if Serial no longer owns it.

## Application shutdown ordering

Current MiniFT8 shutdown closes radio/Serial before destroying RX Audio:

```text
radio_control_close()
app_rx_destroy()
```

T030 must support this order.

Expected normal exit:

```text
Serial close
    -> CDC owner released
    -> Audio still owns shared USB session

Audio close
    -> UAC owner released
    -> final QMX USB host teardown
    -> ADV console restored
```

Startup-failure unwind must also be correct.

No stale handles, leaked tasks, leaked semaphores, or retained USB-host ownership
after a clean FT8 exit.

## ADV FT8 composition defaults

Keep portable `ft8_main.c` platform-independent.

Update ADV packaging in `platform/adv/main/ft8_static.c`.

Normal bare ADV command:

```text
M$> ft8
```

must supply:

```text
--rx  uac:qmx
--cat serial:qmx
```

while preserving explicit user overrides.

Required composition policy:

1. no explicit RX, normal FT8 mode:
   - add `--rx uac:qmx`;
2. default-QMX RX and no explicit CAT:
   - add `--cat serial:qmx`;
3. explicit `--cat ...`:
   - never replace it;
4. explicit non-QMX/WAV `--rx ...`:
   - do not silently add QMX CAT;
5. CAT tone-test mode:
   - do not inject RX;
   - if no explicit CAT is present, ADV may supply `--cat serial:qmx` so a
     concise ADV hardware test is possible.

Do not change Linux defaults.

## No portable MiniFT8 redesign

Do not change the order in portable `ft8_main.c` merely to accommodate ADV.

Today MiniFT8 opens CAT before RX:

```text
app_controller_start_cat()
app_controller_start_rx()
```

ADV must support that order.

This is valuable because receive-safe QMX mode/frequency synchronization occurs
before live RX starts.

## CAT-only receive-safe hardware stage

Before RF TX, ADV must prove receive-safe CAT:

```text
M$> ft8
```

Expected:

- CAT opens;
- QMX receives `MD6; FR0; FT0; FA...`;
- selected MiniFT8 band changes QMX dial frequency;
- no RF keying occurs merely from startup/band changes;
- UAC live decoding continues normally.

This is the first manual hardware acceptance stage.

## TX-stage interaction

The exact production sequence must work on ADV:

```text
live UAC RX
    -> AutoSeq produces TxIntent
    -> physical TX slot due
    -> Audio.stop()        [UAC capture quiesces; CDC remains]
    -> MD6; TX;
    -> TAxxxx.xx for absolute 79-symbol schedule
    -> RX;
    -> Audio.start()
    -> UAC capture resumes
    -> MiniFT8 timing re-establishes
    -> later-slot live decode resumes
```

The existing T029 RX-display behavior also applies:

- prior decoded rows remain visible throughout TX;
- successful TX completion/RX resume clears them.

## Disconnect/error behavior

Preserve conservative cleanup:

- QMX unplug during UAC/CDC operation must not crash;
- a failed CAT transfer returns a MiniShell/FT8 error;
- uncertain TX still owes best-effort `RX;` if the transport remains available;
- no use-after-close of CDC/UAC handles;
- public Serial timeout/error must not silently consume the public handle;
- normal FT8 quit must recover the ADV resident console.

Do not attempt sophisticated hot-plug policy beyond what is needed to preserve
the existing first-attach/reconnect behavior.

## Memory constraints

Cardputer ADV has no PSRAM.

Do not add heap-heavy abstractions.

Reuse existing fixed/static stacks where practical.

Record before/after:

```text
firmware .data/.bss or size summary
shell-ready heap free / largest block
FT8 live heap free / largest block
```

A modest CDC/ownership increase is acceptable, but unexplained large growth is
not.

Do not upgrade the CDC component just to change memory behavior.

## Software tests

Add focused tests where practical for:

### 1. Composition/defaults

Prove ADV packaging produces:

```text
bare ft8              -> uac:qmx + serial:qmx
explicit CAT          -> preserved
explicit WAV RX       -> no implicit QMX CAT
CAT tone-test         -> no implicit RX
```

### 2. Serial boundary

Architecture/static tests must prove:

- ADV Serial provider is platform-owned;
- MiniFT8 still sees only `mini_serial_api_t`;
- QMX CAT strings remain only in MiniFT8 radio code;
- no ESP-IDF/CDC headers enter portable app sources.

### 3. Shared ownership/lifecycle

Use the smallest practical state-level/mock tests to prove:

- Serial-first acquisition is legal;
- Audio then joins the same session;
- Audio.stop does not release Serial/CDC;
- Audio.start resumes without reopening public Serial;
- Serial close while Audio open leaves Host alive;
- final Audio close tears down Host;
- Audio-only RX still works;
- cleanup is idempotent/bounded.

Do not fake the actual hardware CDC transfer as sufficient proof; real ADV
validation is required.

## Automated gates

Run at minimum:

```bash
git status --short

cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure

cmake -S tests/unit -B /tmp/T030-build-unit
cmake --build /tmp/T030-build-unit -j"$(nproc)"
ctest --test-dir /tmp/T030-build-unit --output-on-failure

PYTHONDONTWRITEBYTECODE=1 python3 tests/app_dependency_boundary.py . ft8
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_platform_boundary.py . ft8
PYTHONDONTWRITEBYTECODE=1 python3 tests/ft8_platform_boundary.py .
PYTHONDONTWRITEBYTECODE=1 python3 tests/architecture_rules.py .

source ~/projects/esp-idf/export.sh
idf.py -C platform/adv build

git diff --check
```

The known unrelated `linux_serial_unit` PTY timeout flake remains deferred.
If it alone fails with the already-recorded line-67 timeout assertion, document
it and rerun that isolated test. Do not fix it in T030.

## Hardware validation — staged

Hardware validation is required for T030 completion.

### H1 — boot / resource baseline

Flash current firmware and record:

```text
free
largest block
firmware size
```

Confirm shell/display/keyboard/storage still work.

### H2 — receive-safe CAT + UAC coexistence

Connect QMX by USB-C and run:

```text
M$> ft8
```

Confirm:

- QMX CDC opens;
- QMX UAC opens;
- band selection changes QMX dial frequency;
- no RF keying from synchronization/band changes;
- live FT8 messages decode;
- repeated band changes keep decoding alive.

### H3 — Audio-stop / CDC-survival proof

Trigger a controlled TX attempt or dedicated diagnostic and prove:

- UAC stream stops;
- CDC remains connected;
- CAT write succeeds while UAC is stopped;
- UAC restarts after `RX;`;
- next receive slot decodes normally.

### H4 — short CAT tone sanity check

Only after H2/H3.

Use the existing CAT test path with a short duration, for example 1500 Hz and
approximately 500 ms.

Confirm QMX keys, emits the requested tone, returns to RX, and ADV remains
responsive.

Do not repeat unnecessarily.

### H5 — integrated FT8 on-air TX

Use normal MiniFT8:

- CQ or reply to a live station;
- confirm QMX physical keying;
- complete all 79 absolute symbol intervals;
- confirm `RX;` restoration;
- confirm UAC RX recovery;
- confirm subsequent live decode;
- inspect RT T/R records.

### H6 — first ADV QSO

Complete a real two-way FT8 QSO with Cardputer ADV + QMX.

Capture the RT trace showing the exchange.

This is the final T030 hardware milestone.

### H7 — cleanup / ownership

After FT8 exit:

- resident MiniShell returns normally;
- USB/debug console ownership is restored;
- `usbmsc` still works;
- repeated `ft8 -> quit -> ft8` works;
- no material heap loss across repeated cycles.

## Acceptance criteria

- [x] portable `apps/ft8/**` production code is unchanged;
- [x] no MiniShell public API change;
- [x] no USB Audio OUT / MiniShell Audio TX work is added;
- [x] ADV exposes MiniShell Serial WRITE service for `serial:qmx`;
- [x] provider uses existing QMX CDC interface 0;
- [x] no second USB Host stack is created;
- [x] Serial can open before Audio;
- [x] UAC and CDC share one ADV QMX USB session;
- [x] Audio.stop quiesces UAC only and preserves CDC Serial;
- [x] Audio.start resumes UAC without reopening public Serial;
- [x] final-owner teardown restores console ownership;
- [x] bare ADV `ft8` defaults to both `uac:qmx` and `serial:qmx`;
- [x] explicit WAV RX does not silently open QMX CAT;
- [x] CAT test mode can run without implicit RX;
- [ ] receive-safe CAT band synchronization works on real ADV/QMX;
- [ ] live UAC decode continues with CAT enabled;
- [ ] physical CAT TX works while UAC is paused;
- [ ] 79-symbol FT8 TX completes on real ADV/QMX;
- [ ] RX/UAC recovery works after TX;
- [ ] T028/T029 UI order/lifetime behavior remains correct;
- [ ] RT log records ADV transmissions/receptions correctly;
- [ ] repeated FT8 lifecycle is clean;
- [ ] post-FT8 `usbmsc` still works;
- [ ] first real ADV/QMX two-way FT8 QSO completed;
- [x] no portable MiniFT8 platform dependency added;
- [x] Linux regression suite remains green aside from documented serial flake;
- [x] portable units pass;
- [x] architecture checks pass;
- [x] ADV build passes;
- [x] `git diff --check` passes;
- [x] no unrelated cleanup.

## Non-goals

Do not implement:

- a new CAT protocol;
- CAT response parsing;
- general USB-device discovery UI;
- stable Linux endpoint discovery;
- Wi-Fi/BLE control;
- FT4;
- AutoSeq redesign;
- TX encoder redesign;
- TX timing redesign;
- new public MiniShell APIs;
- CDC component upgrades;
- Linux Serial flake fix;
- arbitrary USB CDC devices;
- QMX/QDX UAC OUT;
- MiniShell Audio TX changes for USB radios;
- QDX support;
- broad extraction/refactoring of the ADV USB stack when the existing
  `adv_audio_uac.cpp` can be extended cleanly.

T030 is specifically QMX CDC transport on ADV plus integrated physical FT8 TX.

## Branch workflow

Use:

```text
codex/T030-adv-qmx-cat
```

Codex:

1. read T019-T024, T028-T029 and current ADV UAC implementation;
2. reproduce that ADV currently has no public Serial service;
3. inspect the existing private CDC lifecycle in `adv_audio_uac.cpp`;
4. implement the smallest clean shared QMX USB-session ownership split;
5. expose `serial:qmx`;
6. update ADV FT8 composition defaults;
7. add focused lifecycle/composition/boundary tests;
8. run all software gates;
9. record memory/size deltas;
10. set Status to REVIEW;
11. push one reviewable implementation commit;
12. return exact SHA;
13. no PR;
14. no Actions wait.

Codex does not perform RF validation. Supervisor review precedes H1-H7 hardware
testing.

## Supervisor scope decision

Final T030 scope agreed with architect:

```text
portable MiniFT8 changes     NONE
MiniShell public API changes NONE
ADV USB Audio IN             keep/use existing QMX UAC-IN path
ADV Serial                   add serial:qmx over existing CDC handle
ADV Audio RX stop/start      logical pause/resume; keep shared USB session alive
ADV USB Audio OUT            NOT IN T030
QDX                          deferred
```

Prefer a minimal patch over abstraction work.

## Codex implementation notes

### Implementation summary

Implemented the minimal shared QMX session in the existing ADV UAC provider.
ADV now publishes Serial WRITE for `serial:qmx` using the existing QMX CDC
interface 0. Audio pause/resume keeps the USB session and CDC handle alive.
The ADV wrapper supplies QMX RX/CAT defaults without changing portable MiniFT8.
No new USB abstraction, component upgrade, UAC OUT, QDX, or speaker change.

Primary hardware reference inspected locally with `git show` at MiniFT8-V2
`491e757ae6b1e4cfd2b9a6ba10f48b35643849e0`: `main/stream_uac.cpp`,
`main/stream_uac.h`, `main/radio_control_qmx.cpp`, `main/radio_control.cpp`,
and `main/main.cpp`. Retained VID/PID 0483:a34c, interface 0, FIFO 91/18/91,
CDC stack 3072/priority 4, disabled CDC RX, and blocking CDC TX. V2's QMX
TX path mutes RX presentation without tearing down the UAC session.

### Files changed

- `platform/adv/adv_audio_uac.cpp`: shared ownership, Serial callbacks,
  CDC lifetime mutex, logical RX pause/resume and safe ring detachment.
- `platform/adv/main/ft8_static.c`: ADV RX/CAT defaults and tone-test handling.
- `tests/adv_uac_allocation_test.py`: execute production ownership/RX callbacks
  with allocation, cleanup, and stale-sample faults.
- `tests/adv_qmx_serial_test.py`: execute production Serial/session callbacks
  with mocked CDC, clock, and semaphore operations; assert worker lock ordering.
- `tests/adv_ft8_defaults_test.py`: execute the ADV wrapper against a recording
  entry point, including overrides, malformed options, and argument bounds.
- `tests/adv_usb_console_boundary.py`: update the old physical-stop assumption
  to require continued draining of an already-running stream during logical stop.
- `CMakeLists.txt`: register the two new tests.
- This task packet: review handoff and evidence.

### Behavior/invariants preserved

`apps/ft8/**`, `include/minishell/api.h`, public service semantics, Linux defaults,
CAT command ownership, persisted formats, UI, TX scheduling/encoding, and the
local speaker provider are unchanged. One host and one instance of each class
driver remain. The pinned UAC 1.3.3 and CDC 2.2.0 dependencies are unchanged.
No production deviation from the final supervisor scope.

### Shared USB ownership design

Foreground Audio and Serial ownership are distinct from session readiness and
incomplete cleanup. Serial may acquire the session without allocating an Audio
ring; Audio joins it later. Either close order works. Audio close detaches/frees
its ring under the capture lock; callbacks tolerate a missing ring. A generation
check prevents an in-flight worker read from feeding a newly allocated ring.

Only final-owner release joins workers, closes devices, uninstalls classes/host,
and restores the console. Worker completion waits now have five-second bounds;
failed cleanup retains infrastructure for retry on the next acquisition without
retaining consumed public handles. Existing class-close/host cleanup retry order
and console handoff are preserved. No new worker or worker-stack allocation.

### Serial provider behavior

`serial:qmx` only, WRITE only, no RX parser or protocol commands. Open waits up to
three seconds for asynchronous CDC readiness after session preparation; absent
CDC unwinds only an otherwise-unowned session. An existing Audio owner survives
a failed Serial open. Public single-open enforcement remains resident-owned.

The new session mutex covers worker open/close and foreground blocking TX,
preventing CDC use-after-close. Disconnect callbacks only flag loss; the worker
closes/reconnects under that mutex. Writes subtract mutex wait from the finite
transport budget, report full count on success and zero on error, and never
consume the public handle or retry an ambiguous transfer.

The pinned driver's millisecond-to-tick multiplication is 32-bit. Large waits
are capped at `UINT32_MAX / configTICK_RATE_HZ` to avoid overflow; for
`MINI_WAIT_FOREVER`, mutex acquisition waits indefinitely and a driver watchdog
expiry reports I/O error instead of retrying potentially transmitted bytes.
Normal MiniFT8 CAT timeouts are much shorter. Actual USB cancellation/cleanup
latency remains driver-dependent and requires hardware evidence.

### Audio stop/start behavior

Audio.stop atomically disables delivery and invalidates/clears the RX ring;
it does not call session release. An already-streaming UAC device keeps draining,
with paused samples discarded. Audio.start invalidates in-flight paused reads.
The existing discontinuity acknowledgment/reset path flushes native queued data
before fresh samples can reach MiniFT8; it may restart the UAC stream during that
reset, without touching CDC or USB Host. First Audio open also requests a fresh
boundary, including when joining a session retained by Serial.

H3's wording about a stopped UAC stream should be interpreted as **logical RX
stopped**, consistent with the final scope and preferred V2 drain/discard policy.
The hardware capture stream can stay active throughout the TX slot.

### Composition/default behavior

Bare `ft8` adds `--rx uac:qmx --cat serial:qmx`. Explicit CAT remains intact.
Explicit `uac:qmx` RX receives the CAT default; WAV/non-QMX RX does not.
CAT tone-test options suppress implicit RX and receive the CAT default when no
explicit CAT or RX was given. Malformed explicit options remain for the portable
parser to reject. No Linux packaging changes.

### Failing-before / passing-after evidence

The new wrapper regression run against the saved task-head `ft8_static.c`
failed its recorded argument-count assertion on bare `ft8` (missing CAT default).
The same test passes against the implementation.

Before: ADV configured no Serial callbacks and Audio.stop called `release()`.
After: executable mock tests prove Serial-first acquisition, same-session Audio
join, both close orders, audio-only operation, pause/resume without host release,
failed allocation while Serial remains active, idempotent final release, cleanup
failure/retry, invalid endpoints, absent CDC, count/error semantics, finite lock
budget, disconnect/reconnect, and post-error handle reuse. RX tests verify
stopped reads yield NOT_READY, resume yields discontinuity, old-epoch samples
are rejected, and fresh samples become readable after reset acknowledgment.

The previous allocation test expected stop to uninstall USB and failed close to
retain the Audio ring/public reservation. Those assumptions are intentionally
updated for shared ownership and consumed-on-close handles; fault coverage is
retained and expanded. Mock transport success is not hardware proof.

### Memory/size evidence

Built task head `c20268b` and the implementation with the same local ESP-IDF 5.5
toolchain/configuration. `idf.py -C platform/adv size`:

| Metric | Before | After | Delta |
| --- | ---: | ---: | ---: |
| DIRAM .data | 19,304 B | 19,304 B | 0 B |
| DIRAM .bss | 16,864 B | 16,880 B | +16 B |
| Flash code | 542,810 B | 544,690 B | +1,880 B |
| Flash data | 156,476 B | 156,524 B | +48 B |
| Total image | 772,249 B | 774,177 B | +1,928 B |

One session-lifetime mutex is the additional runtime allocation. Existing ring
capacity, static capture stack, dynamic worker stacks, and class buffers remain
unchanged. Serial-only startup needs no canonical Audio ring.

Shell-ready and FT8-live free heap/largest block **not measured locally**: no
Cardputer/QMX hardware run was performed. Record matched before/after hardware
measurements in H1/H2/H7; firmware section sizes are not a substitute.

### Local tests run

```bash
git status --short
# Clean at task start; final changes limited to the eight files listed above.

cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure
# PASS 61/61, including linux_serial_unit; no flake retry needed.

cmake -S tests/unit -B /tmp/T030-build-unit
cmake --build /tmp/T030-build-unit -j"$(nproc)"
ctest --test-dir /tmp/T030-build-unit --output-on-failure
# PASS 15/15.

PYTHONDONTWRITEBYTECODE=1 python3 tests/app_dependency_boundary.py . ft8
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_platform_boundary.py . ft8
PYTHONDONTWRITEBYTECODE=1 python3 tests/ft8_platform_boundary.py .
PYTHONDONTWRITEBYTECODE=1 python3 tests/architecture_rules.py .
# All PASS.

source ~/projects/esp-idf/export.sh
idf.py -C platform/adv build
idf.py -C platform/adv size
# PASS baseline and implementation. Initial sandbox build could not access
# component registry; authorized network-enabled baseline build succeeded.

PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux \
  -R 'adv_uac_allocation|adv_qmx_serial' --output-on-failure
# PASS 2/2 after adding explicit stale-sample/allocation-fault coverage.

git diff --check
# PASS.
```

### Hardware validation still required

All H1–H7 remain pending supervisor review: shell/live heap measurements,
receive-safe band synchronization with concurrent live decoding, logical RX
pause with CDC survival, controlled tone test, absolute 79-symbol TX/RX recovery,
first two-way ADV QSO/RT trace, repeated lifecycle/unplug behavior, restored
console and post-FT8 `usbmsc`. No RF validation or flash operation was performed.

### Known limitations / risks

Host mock tests do not emulate ESP-IDF scheduling, real USB disconnects, endpoint
cancellation, QMX command acceptance, or RF timing. Hardware validation remains
the completion gate. A driver cleanup failure can retain the USB lease until
retry; the console is never restored over a still-installed host/class driver.
The initial implementation required another open after the three-second CDC
readiness deadline; R1 below supersedes that limitation for normal ADV FT8
startup. No general device discovery or CAT response parser is added. See
Serial watchdog detail above.

### Commit

One implementation commit on `codex/T030-adv-qmx-cat`, based on task head
`c20268b`. The commit containing these notes is the implementation reference;
its exact SHA is returned in the Codex handoff. No PR or GitHub Actions wait.

## Supervisor review

Review of implementation commit `1837f476a3ccf803ab61420afc3999332f4723d6`:

Production scope and architecture PASS:

- no `apps/ft8/**` production changes;
- no public MiniShell API changes;
- QMX UAC-IN + CDC only; no UAC OUT/QDX;
- Serial WRITE is `serial:qmx` over the existing CDC interface-0 handle;
- one shared USB Host/class-driver session;
- Audio RX stop/start is logical pause/resume;
- RX generation guard prevents stale/in-flight samples reaching a replaced ring;
- CDC lifetime mutex covers worker open/close versus foreground blocking TX;
- final-owner release restores console ownership;
- ADV wrapper defaults are correctly isolated from Linux;
- software/ADV-build evidence is acceptable.

One required change before hardware validation:

### R1 — preserve disconnected-start / late first attach

The accepted ADV/T017 behavior allows MiniFT8 to start with QMX absent and later
accept the first QMX attachment. T030 also explicitly says to preserve existing
first-attach/reconnect behavior.

Current T030 behavior regresses that path:

```text
bare ft8
  -> ADV wrapper adds --cat serial:qmx
  -> portable ft8 opens CAT before RX
  -> serial_open starts the QMX session and waits at most 3 s for CDC
  -> no QMX => MINI_ERR_NOT_READY
  -> app_controller_start_cat fails
  -> ft8 exits before Audio RX opens
```

The implementation note currently documents this as a limitation:

```text
CDC readiness wait is three seconds, so a radio attached later requires another
open if no Audio owner keeps discovery alive.
```

For bare ADV `ft8`, there is no Audio owner yet because CAT opens first, so the
existing disconnected-start behavior is lost.

Fix this **without changing portable MiniFT8** and without pretending unsent CAT
commands succeeded. Prefer the smallest ADV-platform/composition solution that
retains truthful MiniShell Serial semantics and the proven V2-style persistent
USB discovery/session.

Add a regression proving:

```text
bare ADV ft8 + QMX absent initially
    -> application remains usable/waiting rather than returning CAT startup error
    -> first QMX attachment can establish CDC + UAC
    -> receive-safe CAT synchronization occurs
    -> live RX can then begin
```

If preserving this behavior is impossible without materially broadening T030,
document the exact constraint for supervisor/architect review rather than hiding
the regression as a known limitation.

All other reviewed implementation areas are approved pending R1 and hardware
H1-H7.

## Codex R1 implementation notes

### Implementation summary / files changed

Addressed R1 with a cancellable ADV composition wait before portable CAT-first
startup. `platform/adv/main/ft8_static.c` validates the composed options using
the existing parser, then normal `serial:qmx` startup polls readiness while
showing `ft8: waiting for QMX` and `Q/Esc: cancel`. No first-attach deadline is
imposed by this composition wait. Tone tests, malformed options, non-QMX CAT,
and RX without QMX CAT bypass the wait and preserve their previous behavior.

`platform/adv/adv_audio_uac.cpp` adds two private foreground composition helpers,
declared only in `platform/adv/adv_internal.h`: prepare/poll the existing shared
session without opening a public handle, and release it if no public owner
remains. NOT_READY retains USB discovery. On readiness the portable entry opens
Serial, performs actual CAT synchronization, and starts RX normally. Every
wrapper exit pairs preparation with release, including cancellation, missing
input, driver failure, and portable initialization failure before CAT ownership.
There is no new session abstraction, worker, owner flag, or runtime allocation.

`tests/adv_ft8_defaults_test.py` now uses the real portable option parser and
checks which composed invocations prepare/release the waiting session.
`tests/adv_qmx_serial_test.py` adds the late-attach and unwind regression described
below. This task packet is the sixth changed file.

### Behavior/invariants preserved

No `apps/ft8/**` or public API changes. Serial.open still has its bounded readiness
wait, and writes still report actual CDC transfer success/error. No unsent CAT
command is accepted, buffered, or retried by composition. Commands remain in the
existing portable radio adapter. UAC-IN/CDC ownership, Audio pause/resume,
component pins, Linux defaults, speaker, UAC OUT exclusion, and QDX exclusion
are unchanged. The wait screen is ADV composition; the portable FT8 UI begins
after attachment. This implements the review's allowed usable/waiting behavior
without broadening portable application startup policy.

### Failing-before / passing-after regression evidence

The executable regression links the **actual** ADV wrapper, portable parser,
`radio_control.c`, `radio_qmx.c`, and production ADV session/Serial/Audio lifecycle
callbacks. The application entry is a small harness performing CAT-open/sync
then Audio-open/start in the existing portable order. USB discovery, CDC transfer,
keyboard, clock, heap, and driver preparation/release are mocked.

Bare `ft8` starts with no CDC device. Forty 100-ms input waits assert that the
same session remains alive, no app-entry/CAT write has occurred, and the wait
message is shown once. First attachment at four seconds then permits real radio
adapter synchronization (`MD6; FR0; FT0; FA00007074000;`) before RX opens/starts.
Exactly one preparation and one final release occur. Additional cases cover Q
and Escape cancellation, input failure, preparation failure, unavailable CDC
worker, portable pre-CAT initialization failure, and failed CAT synchronization
without false write success or RX startup. Existing Serial/lifecycle fault tests
remain intact.

Running that regression with the previous wrapper saved from `f6b8950` fails the
normal-return assertion: it reaches the old CAT startup error instead of waiting
for late attach. The revised wrapper passes all cases.

### Tests run and results

```bash
git status --short
# Clean before R1 implementation.

cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure
# PASS 61/61; no linux_serial_unit flake.

cmake -S tests/unit -B /tmp/T030-build-unit
cmake --build /tmp/T030-build-unit -j"$(nproc)"
ctest --test-dir /tmp/T030-build-unit --output-on-failure
# PASS 15/15.

PYTHONDONTWRITEBYTECODE=1 python3 tests/app_dependency_boundary.py . ft8
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_platform_boundary.py . ft8
PYTHONDONTWRITEBYTECODE=1 python3 tests/ft8_platform_boundary.py .
PYTHONDONTWRITEBYTECODE=1 python3 tests/architecture_rules.py .
# All PASS.

source ~/projects/esp-idf/export.sh
idf.py -C platform/adv build
idf.py -C platform/adv size
# PASS. R1 image 774741 B (+564 B versus reviewed T030 implementation).
# DIRAM .data 19304 B and .bss 16880 B both unchanged.

PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux \
  -R 'adv_ft8_defaults|adv_qmx_serial' --output-on-failure
# PASS 2/2 after adding composition probe-count assertions.

git diff --check
# PASS after removing a trailing space in the test harness.
```

### Hardware/manual validation still required

H1–H7 remain pending. In particular, start bare `ft8` with QMX unplugged, wait
longer than three seconds, attach it, and verify receive-safe frequency/mode
synchronization followed by live decoding. Also verify Q/Escape cancellation
restores the resident console and post-cancel `usbmsc`, then retry FT8. No
hardware/RF test or flashing was performed. Shell/live heap free and largest
block remain hardware measurements; R1 adds no runtime allocation.

### Known limitations / risks

The host regression proves composition and callback ordering, not actual USB
attachment timing or DSP decoding. A fresh unplug between readiness detection
and portable CAT synchronization can still produce a truthful startup transport
error; R1 does not add automatic retries of ambiguous commands. Driver/cleanup
failures remain reported, with the existing retained-session cleanup retry.

### Commit reference

One new implementation commit on `codex/T030-adv-qmx-cat`, based on supervisor
review head `f6b8950`. The commit containing this R1 handoff is the reference;
its exact SHA is returned to the architect. Task returned to REVIEW. No PR or
GitHub Actions wait.

### R1 supervisor re-review — PASS

Reviewed R1 implementation commit:

```text
51e45d631d285fb7080c2bee643b1a90a292617e
```

R1 satisfies the disconnected-start requirement without changing portable
MiniFT8 or public MiniShell APIs.

Accepted behavior:

```text
bare ADV ft8, QMX absent
    -> ADV composition starts/retains the existing private QMX USB discovery session
    -> wait UI remains active indefinitely
    -> Q/Esc can cancel cleanly
    -> no public Serial handle exists yet
    -> no CAT command is reported/sent as successful

first QMX attachment
    -> CDC becomes genuinely ready
    -> portable ft8 entry starts
    -> public serial:qmx opens normally
    -> existing radio_qmx performs MD6/FR0/FT0/FA synchronization
    -> only then does portable RX open/start uac:qmx
```

The private `adv_qmx_prepare_serial()/adv_qmx_release_unused()` helpers are
ADV composition-only and do not alter the public API. Repeated NOT_READY polling
retains one existing USB session rather than reinstalling the Host/class drivers.
Cancellation/error paths pair preparation with release. Tone-test and non-QMX
paths preserve their intended behavior.

Regression coverage executes the actual ADV wrapper plus the real portable
option parser and radio_qmx synchronization, including a simulated first attach
after four seconds, which is beyond the original three-second Serial-open
deadline.

Software gates remain PASS:

```text
Linux CTest       61/61
portable units    15/15
architecture      PASS
ADV build         PASS
diff check        PASS
```

R1 image growth is +564 B versus the first reviewed T030 implementation, with
DIRAM .data/.bss unchanged.

Supervisor review is complete. Proceed to hardware H1-H7.

## Architect test result

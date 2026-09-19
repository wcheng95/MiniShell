# T030 — ADV QMX CAT + shared UAC/CDC USB session + first ADV QSO

Status: READY

## Architect intent

Bring the proven Linux/QMX CAT + physical FT8 TX boundary to Cardputer ADV so
ADV can make its first real MiniFT8-V3 QSO.

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
    +-- UAC capture -> MiniShell Audio -> MiniFT8 RX
    |
    `-- CDC interface 0 -> MiniShell Serial -> radio_qmx CAT
                                   |
                                   +-- MD6 / FR0 / FT0 / FA...
                                   +-- TX
                                   +-- TA tone updates
                                   `-- RX
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

The current ADV UAC provider already owns the correct shared QMX USB host,
UAC driver, CDC driver, capture task, CDC task, and console handoff. Extend that
existing owner so MiniShell Audio and Serial can share it safely.

Minimum state distinction needed:

```text
QMX USB session alive
Audio public owner/open
Audio logical started/stopped
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

## UAC Audio lifecycle change — preserve V2 concurrent USB behavior

Keep endpoint:

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

- [ ] ADV exposes MiniShell Serial WRITE service for `serial:qmx`;
- [ ] provider uses existing QMX CDC interface 0;
- [ ] no second USB Host stack is created;
- [ ] Serial can open before Audio;
- [ ] UAC and CDC share one ADV QMX USB session;
- [ ] Audio.stop quiesces UAC only and preserves CDC Serial;
- [ ] Audio.start resumes UAC without reopening public Serial;
- [ ] final-owner teardown restores console ownership;
- [ ] bare ADV `ft8` defaults to both `uac:qmx` and `serial:qmx`;
- [ ] explicit WAV RX does not silently open QMX CAT;
- [ ] CAT test mode can run without implicit RX;
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
- [ ] no portable MiniFT8 platform dependency added;
- [ ] no public MiniShell API change;
- [ ] Linux regression suite remains green aside from documented serial flake;
- [ ] portable units pass;
- [ ] architecture checks pass;
- [ ] ADV build passes;
- [ ] `git diff --check` passes;
- [ ] no unrelated cleanup.

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

## Codex implementation notes

### Implementation summary

### Files changed

### Shared USB ownership design

### Serial provider behavior

### Audio stop/start behavior

### Composition/default behavior

### Failing-before / passing-after evidence

### Memory/size evidence

### Local tests run

### Hardware validation still required

### Known limitations / risks

### Commit

## Supervisor review

Review exact task-head to implementation diff.

## Architect test result

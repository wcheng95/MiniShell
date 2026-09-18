# T017 — ADV QMX USB-host UAC RX vertical slice

Status: TESTING

## Objective

Bring the working MiniFT8-V2 QMX USB host path into MiniShell's ADV backend without
reintroducing V2 platform coupling.

End-to-end acceptance target:

```text
QMX USB composite device
    UAC IN: 48 kHz / 24-bit / stereo
    CDC-ACM: present if available
        |
        v
ADV MiniShell USB-host Audio RX provider
        |
        v
MiniShell Audio contract
12 kHz / S16 / stereo
        |
        v
existing MiniFT8 V3
rx_audio_adapter
-> RxFrontend
-> RxSlotFramer
-> Ft8Engine
-> RxResultBuilder
-> RX UiModel
        |
        v
live decoded FT8 messages on ADV RX screen
```

User-facing first milestone:

1. tune QMX manually to an FT8 frequency;
2. connect QMX USB to Cardputer ADV host;
3. at `M$>`, run `ft8` with no RX argument;
4. ADV defaults that packaged FT8 instance to the QMX UAC endpoint;
5. decoded messages appear on RX for consecutive FT8 slots.

No physical TX is part of T017.

## Reference implementation

Use MiniFT8-V2 only at the approved pinned reference:

```text
repository: wcheng95/Mini-FT8
commit:     491e757ae6b1e4cfd2b9a6ba10f48b35643849e0
```

Primary reference files:

```text
main/stream_uac.cpp
main/stream_uac.h
main/resample.cpp
main/radio_control_qmx.cpp
main/CMakeLists.txt
main/idf_component.yml
dependencies.lock
```

V2 resolved components:

```text
espressif/usb_host_uac      1.3.3
espressif/usb_host_cdc_acm  2.2.0
ESP-IDF                     5.5.1
```

Current MiniShell ADV uses ESP-IDF 5.5.4. Start from the V2-resolved UAC/CDC component
versions unless the current component manager requires a narrowly documented
compatibility adjustment.

## What to reuse from V2

Reuse/adapt the proven platform mechanics:

- ESP32-S3 USB Host Library install/uninstall;
- QMX USB FIFO partition:
  - RX FIFO lines = 91
  - non-periodic TX FIFO lines = 18
  - periodic TX FIFO lines = 91
- UAC host driver install/uninstall;
- UAC RX-connected event handling;
- strict QMX stream format:
  - 48000 Hz
  - 24-bit
  - 2 channels
- QMX VID/PID:
  - VID 0x0483
  - PID 0xA34C
- V2 2304-byte DMA read block (8 x 1 ms transfers);
- orderly disconnect/driver cleanup;
- draining USB host events before `usb_host_uninstall()`;
- CDC-ACM host install/open/disconnect handling where compatible;
- QMX CDC interface 0 fast path plus descriptor/hint fallback if useful.

## What NOT to copy from V2

Do not copy V2's application/platform coupling:

- no direct FT8 decode from `stream_uac.cpp`;
- no `ft8_audio_pipeline_run()`;
- no V2 resample/DSP ownership in the USB backend;
- no waterfall/UI calls;
- no AutoSeq state;
- no V2 global application state;
- no UAC OUT/TX/DDS path in this task;
- no QMX CAT band/mode commands from the Audio provider;
- no application includes of USB/ESP-IDF headers.

The ADV backend transports canonical audio. MiniFT8 V3 owns all FT8 DSP above the
MiniShell Audio API.

## Architectural ownership

```text
QMX UAC / ESP-IDF USB host mechanics
    platform/adv only

native 48k/S24_3LE/stereo -> canonical 12k/S16/stereo
    ADV Audio RX provider

continuous acquisition while decoder is busy
    ADV Audio RX provider worker/ring

12k stereo -> 6k mono
    existing RxFrontend

slot timing / framing
    existing RxSlotFramer

FT8 DSP / protocol decode
    existing Ft8Engine

decoded-list presentation
    existing app_controller + ui_shell
```

No new public MiniShell API is needed for UAC RX.

## Endpoint

Define one explicit initial ADV UAC RX endpoint:

```text
uac:qmx
```

For T017 it means the proven QMX profile only.

Do not implement a broad generic-UAC policy unless it is nearly free after the QMX
vertical slice works.

Existing ADV WAV endpoints must continue to work.

## ADV Audio RX provider composition

ADV currently has a Filesystem-backed WAV RX provider.

Do not replace it.

Preferred composition:

1. existing WAV provider configures the base Audio RX callback set;
2. new UAC provider wraps/dispatches:
   - `uac:qmx` -> UAC provider;
   - other endpoints -> existing WAV provider;
3. provider/backend handles remain unambiguous.

Linux already demonstrates this layered provider style with WAV/ALSA/buffering.

A small private dispatcher/wrapper is preferred over teaching the portable Audio
service about endpoint kinds.

## Canonical conversion

QMX native input:

```text
48000 Hz
signed 24-bit packed little-endian
2 channels
6 bytes/native frame
```

MiniShell public stream requested by MiniFT8:

```text
12000 Hz
signed 16-bit
2 channels
4 bytes/canonical frame
```

Conversion must preserve channel order:

1. sign-extend each 24-bit LE channel;
2. convert S24 -> S16 with arithmetic shift by 8;
3. decimate 48 kHz -> 12 kHz by factor 4 with a persistent phase across USB reads;
4. emit interleaved L/R S16 frames.

This should match the already-accepted Linux QMX provider behavior.

Do not downmix in the ADV provider; `RxFrontend` owns stereo channel meaning and
12 kHz -> 6 kHz conversion.

## Continuous capture requirement

This is mandatory.

MiniFT8 V3 decodes synchronously. During decode the foreground ADV app task may stop
calling `Audio.read()`.

Therefore UAC acquisition must continue in an ADV worker/task below MiniShell Audio,
just as Linux keeps ALSA capture running below the application.

Task priority guidance from the proven V2 structure:

- USB host / class work higher than foreground application;
- capture worker should not be starved by synchronous decode;
- ADV foreground app currently runs at `tskIDLE_PRIORITY + 1`.

Do not make MiniFT8 spawn or own a USB capture task.

## Canonical ring buffer

Store already-converted MiniShell canonical frames in the ring:

```text
12 kHz / S16 / stereo
```

not 48 kHz / 24-bit native frames.

Current architect-confirmed target after the lazy-ring allocation failure:

```text
2048 canonical frames
= 8192 bytes
= about 170.7 ms at 12 kHz
```

This supersedes the earlier 16384-frame confirmation following measured allocation
failure. Lazy allocation remains unchanged. Historical implementation and review
notes below retain their original capacities; the 2048-frame handoff records the
current build. Further capacity changes require measurements and authorization.

Use a power-of-two ring and fixed/static storage where practical.

This is a starting point, not permission to grow RAM blindly. Record high-water
occupancy during hardware validation. If it overflows during decode, measure the
decode/capture pause and adjust only with evidence.

The V2 DMA read block remains small:

```text
2304 bytes
= 384 native stereo frames
= 8 ms
= 96 canonical 12 kHz frames after decimation
```

## Discontinuity behavior

The UAC provider must not silently bridge lost audio.

If any of these occur:

- UAC transfer failure that loses continuity;
- ring overflow;
- disconnect/reconnect;
- provider reset that discards captured frames;

then the next consumer observation must produce:

```text
MINI_ERR_DISCONTINUITY
```

and flush/restart the canonical capture epoch.

The existing MiniFT8 path already handles this by:

- resetting RxFrontend stream phase;
- marking RX timing pending;
- re-anchoring slot timing from UTC on the next real samples.

Reuse the accepted Linux discontinuity ownership pattern where practical.

## Open/start/read/stop/close lifecycle

Desired semantics:

### open("uac:qmx")

- reserve provider state;
- prepare/install USB host + UAC class infrastructure;
- installing CDC companion infrastructure is allowed here;
- QMX does not need to be physically present yet;
- return a valid Audio handle if provider infrastructure is ready.

### start()

- clear canonical ring/epoch;
- enable capture worker;
- if QMX is not yet enumerated, remain a valid started stream;
- reads may return `MINI_ERR_NOT_READY` until UAC RX is connected.

### read()

- consume canonical frames from the ring;
- respect public timeout semantics as already documented;
- `MINI_WAIT_NONE` must not intentionally block;
- return `MINI_ERR_NOT_READY` while waiting for first QMX stream;
- return `MINI_ERR_DISCONTINUITY` once per continuity-loss epoch;
- never expose native UAC bytes.

### stop()/close()

- stop capture producer;
- stop/close UAC device;
- close CDC handle if open;
- uninstall CDC/UAC class drivers;
- drain USB host events;
- uninstall USB Host Library;
- release provider state and ring;
- leave the OTG peripheral reusable by the existing `usbmsc` command.

Repeated `ft8 -> quit -> ft8` must work without reboot.

## CDC-ACM companion scope

Include CDC host support in T017 if the V2 component/API works on current IDF 5.5.4.

Expected behavior:

- install CDC-ACM host on the same USB Host Library;
- detect/open QMX CDC interface 0 using V2 VID/PID first;
- retain disconnect handling;
- log/diagnose CDC ready state;
- CDC failure must NOT prevent UAC RX from decoding.

T017 does NOT send FT8/QMX policy commands over CDC.

Specifically do not copy V2's:

```text
MD6;
FR0;
FT0;
FA...........;
RX;
TX;
TA....
```

into the Audio backend.

Those are radio-control semantics and belong in a future generic MiniShell Control/CAT
service task.

If CDC support creates an unexpected component/API conflict, keep UAC as the mandatory
deliverable, record CDC as deferred, and do not contaminate Audio ownership to force it.

## USB PHY ownership, debug console and usbmsc coexistence

This section is an architect decision and is mandatory for the T017 amendment.

The ESP32-S3 internal USB PHY has exactly one MiniShell owner at a time.

Canonical ownership state:

```text
normal M$> shell
    USB Serial/JTAG console active
    GPIO4/5 debug UART inactive
    USB Host inactive
    TinyUSB MSC inactive

enter ft8 using uac:qmx
    suspend/uninstall USB Serial/JTAG
    enable temporary V2-style debug UART
        TX = GPIO4
        RX = GPIO5
        baud = 115200
    install USB Host
    run QMX UAC + optional CDC
    Cardputer display/keyboard remain the authoritative FT8 UI/input

clean ft8 exit
    stop capture
    close UAC device
    close CDC device
    uninstall CDC/UAC class drivers
    drain USB host events
    usb_host_uninstall()
    CONFIRM PHY RELEASED
    disable temporary GPIO4/5 debug UART
    restore USB Serial/JTAG console
    return to normal M$>

usbmsc from M$>
    keep existing usbmsc ownership handoff unchanged:
    suspend USB Serial/JTAG
    run TinyUSB MSC device mode
    stop MSC
    restore USB Serial/JTAG
    return M$>
```

Required implementation rules:

1. Reuse `adv_console_suspend_for_usb()` before `usb_host_install()`.
2. Do not call `usb_host_install()` while USB Serial/JTAG is still installed.
3. Add a private ADV temporary debug-UART owner using the proven MiniFT8-V2 console
   wiring:
   - UART TX GPIO4
   - UART RX GPIO5
   - 115200 baud.
4. The GPIO4/5 UART is a diagnostics/debug terminal during the FT8 USB-host window.
   It is not a new public MiniShell service and not part of MiniFT8 application
   logic.
5. Route ADV/System/debug diagnostics that would otherwise disappear while USB
   Serial/JTAG is suspended to GPIO4/5 during this window.
6. Cardputer Display/Input remain the authoritative FT8 UI/control path.
7. GPIO5 RX may accept debug-terminal input only if it can be done without creating
   competing FT8 UI/application policy. Diagnostic output is mandatory; UART input
   is optional for T017.
8. Restore USB Serial/JTAG only after UAC/CDC are gone and
   `usb_host_uninstall()` has actually released the PHY.
9. If USB Host teardown is incomplete:
   - do NOT re-enable USB Serial/JTAG;
   - keep/report diagnostics through GPIO4/5 where possible;
   - return an error and preserve enough state for a later cleanup retry.
10. If prepare fails after USB Serial/JTAG was suspended, unwind whatever host/class
    ownership was acquired; restore USB Serial/JTAG only when the PHY is confirmed
    free.
11. Repeated `ft8 -> quit -> ft8` must suspend/restore both console owners cleanly.
12. Do not attempt UAC-host and TinyUSB MSC device mode simultaneously.
13. Do not change `usbmsc`'s already validated handoff behavior.

Hardware acceptance therefore includes:

```text
normal M$> visible over USB Serial/JTAG
-> run ft8
-> USB Serial/JTAG disconnects
-> GPIO4 TX shows T017/UAC diagnostics at 115200
-> Cardputer display/keyboard run FT8
-> q
-> UAC/CDC stop
-> USB Host fully uninstalls
-> USB Serial/JTAG enumerates again
-> M$> available again over USB
-> run usbmsc flash
-> PC sees storage
-> eject
-> Q returns to M$>
```

## ADV default FT8 RX

For the statically packaged ADV FT8 application, bare:

```text
M$> ft8
```

should default to:

```text
--rx uac:qmx
```

without teaching MiniFT8 core about ADV hardware.

Preferred implementation is composition-time configuration, for example a generic
`FT8_DEFAULT_RX_ENDPOINT` compile-time default or a narrow ADV wrapper.

Requirements:

- Linux bare `ft8` behavior remains unchanged;
- an explicit `--rx ...` always overrides the ADV default;
- ADV deterministic WAV usage remains possible:
  `ft8 --rx /flash/kfs.wav --rx-slot 12345`;
- no `#ifdef ADV` or USB identifier inside shared MiniFT8 domain modules.

## Component dependencies

Add the smallest required ADV component dependencies.

Prefer the V2-proven resolved versions first:

```yaml
espressif/usb_host_uac: "1.3.3"
espressif/usb_host_cdc_acm: "2.2.0"
```

Do not vendor/copy managed component source into MiniShell unless the component manager
cannot supply the proven version.

Record the actual resolved versions after the build.

Architect-approved scope extension (2026-09-18): include a narrow, reproducible
build-time patch to UAC 1.3.3 that reports silent RX losses through the existing
TRANSFER_ERROR callback. The architect explicitly approved this extension before
implementation. Preserve managed package contents and unrelated driver behavior.

## Private implementation shape

Likely files:

```text
platform/adv/adv_audio_uac.cpp
platform/adv/adv_audio_uac_*.h        # private helper(s), only if useful
platform/adv/adv_backend.c
platform/adv/main/CMakeLists.txt
platform/adv/main/idf_component.yml
platform/adv/main/ft8_static.c        # composition default only
```

Existing `adv_audio_wav.c` may be lightly adapted for RX-provider chaining, but do
not rewrite unrelated WAV logic.

## Host-testable helpers

Extract/test pure private mechanics where useful:

1. S24_3LE stereo -> S16 stereo conversion;
2. persistent factor-4 decimation phase across arbitrary USB chunk boundaries;
3. ring wrap/partial read;
4. no-overwrite/full detection;
5. discontinuity epoch flush/ack;
6. endpoint dispatch;
7. handle routing/delegation to WAV provider;
8. WAIT_NONE / finite timeout decisions at the provider layer where host-testable.

Use deterministic byte vectors, including:

- positive/negative full-scale;
- sign-extension boundaries;
- INT24 min/max;
- independent L/R values;
- transfer boundaries not aligned to the decimation cycle.

Do not unit-test ESP-IDF itself.

## Existing regressions that must stay green

- Linux root CTest: current full green baseline;
- portable unit suite;
- architecture checks;
- ADV WAV RX;
- ADV speaker Audio TX / Keyer;
- ADV runtime ELF;
- usbmsc build path.

## ADV build gate

Use current toolchain:

```bash
cd ~/projects/MiniShell
source ~/projects/esp-idf/export.sh
idf.py -C platform/adv build
```

Record:

- resolved UAC component version;
- resolved CDC component version;
- firmware size;
- free heap/largest block before FT8 where existing diagnostics permit;
- static/BSS increase attributable to UAC ring/buffers if practical.

## Hardware validation — phase 1: enumeration / canonical audio

With QMX connected to ADV USB host:

1. tune QMX manually to an active FT8 dial frequency;
2. boot ADV;
3. run `ft8`;
4. confirm log evidence:
   - USB Host installed;
   - QMX device VID/PID observed;
   - UAC RX interface opened;
   - selected 48000/24/2 format;
   - CDC-ACM opened if implemented;
5. confirm MiniFT8 remains running;
6. record canonical capture diagnostics:
   - ring high-water;
   - overflow/discontinuity count;
   - read/capture errors;
   - heap/largest block if available.

No CAT frequency synchronization is expected in T017, so manual QMX tuning is required.

## Hardware validation — phase 2: live FT8

From bare:

```text
M$> ft8
```

Acceptance:

- RX becomes active from the ADV default `uac:qmx` endpoint;
- decoded FT8 messages appear on RX screen;
- decoding works for at least 3 consecutive 15-second slots;
- the top/UI remains responsive between decode events;
- no UAC ring overflow during normal decode;
- no application error/return to shell;
- no slot drift obvious across consecutive slots.

Record representative decoded lines and timing.

## Hardware validation — phase 3: lifecycle

1. quit FT8 normally to `M$>`;
2. repeat `ft8` live RX at least 3 times;
3. verify no obvious heap/resource leak;
4. unplug/replug QMX once if practical:
   - no crash;
   - discontinuity is reported/recovered;
   - live decode resumes after replug if provider reconnect support is implemented;
5. after quitting FT8, run `usbmsc flash`;
6. confirm MSC works and returns cleanly to MiniShell.

## Success criteria

Mandatory T017 completion (checked items have source/local-test evidence;
unperformed hardware acceptance remains unchecked):

- [x] V2 USB host/UAC mechanics traced and adapted, not blindly copied;
- [x] QMX UAC endpoint `uac:qmx` exists on ADV;
- [ ] strict 48k/24-bit/stereo negotiation works;
- [x] native UAC audio becomes MiniShell 12k/S16/stereo;
- [x] channel order preserved;
- [x] existing RxFrontend remains owner of mono/6k conversion;
- [x] continuous producer drains UAC during synchronous decode;
- [x] ring overflow/USB loss becomes explicit discontinuity;
- [ ] ADV WAV RX remains usable;
- [x] bare ADV `ft8` defaults to live `uac:qmx`;
- [x] explicit `--rx` overrides default;
- [ ] live decoded FT8 messages appear on ADV RX screen for >=3 consecutive slots;
- [ ] FT8 can exit/re-enter repeatedly;
- [ ] USB host teardown permits subsequent `usbmsc`;
- [x] Linux 37/37 baseline remains green;
- [x] portable unit suite remains green;
- [x] ADV firmware builds;
- [x] no public API expansion;
- [x] no FT8 platform dependency;
- [x] no physical TX implementation;
- [x] no unrelated cleanup.

CDC best-effort acceptance:

- [ ] CDC component installs alongside UAC if compatible;
- [ ] QMX CDC interface opens and disconnects cleanly;
- [x] CDC failure does not break UAC RX;
- [x] no CAT policy commands are sent in T017.

## Non-goals

Do not implement:

- physical FT8 TX;
- UAC OUT;
- generic MiniShell Control API;
- QMX CAT frequency/mode/TX commands;
- automatic band-to-radio synchronization;
- generic arbitrary UAC devices;
- USB MSC while UAC host is active;
- UI redesign;
- new FT8 DSP/resampler;
- V2 direct USB-to-decoder coupling.

## Local software test gate

Before hardware:

```bash
git status --short

cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"
ctest --test-dir build-linux --output-on-failure

cmake -S tests/unit -B /tmp/T017-build-unit
cmake --build /tmp/T017-build-unit -j"$(nproc)"
ctest --test-dir /tmp/T017-build-unit --output-on-failure

python3 tests/app_dependency_boundary.py . ft8
python3 tests/app_platform_boundary.py . ft8
python3 tests/ft8_platform_boundary.py .

source ~/projects/esp-idf/export.sh
idf.py -C platform/adv build

git diff --check
```

No GitHub Actions wait.

## Branch workflow

Use:

```text
codex/T017-adv-usb-uac-rx
```

Codex handoff:

1. inspect pinned V2 UAC/CDC code and local current IDF APIs;
2. implement the provider and host lifecycle only within the above ownership;
3. run host/Linux/unit/ADV build gates;
4. set Status to REVIEW;
5. commit and push;
6. return commit SHA;
7. no PR;
8. no Actions wait.

Supervisor reviews the diff before hardware testing.

Do not merge before live ADV QMX decode is confirmed.

## Codex implementation notes

The following records initial implementation commit
`454368fe570e1f2df2efa1505242c50a0f0c4b6d`. The amendment report below supersedes
its console-ownership behavior and supplies the current local regression results.

### Implementation summary / reference mapping

Implemented the bounded ADV QMX UAC RX provider on
`codex/T017-adv-usb-uac-rx`. Inspected all named V2 reference files with
`git show 491e757ae6b1e4cfd2b9a6ba10f48b35643849e0:<path>` in the local Mini-FT8
repository. Adapted FIFO 91/18/91, USB/class priorities, strict QMX 0483:a34c
48000/24/2 profile, 2304-byte DMA buffer, host-event drain and CDC interface 0.
The backend does not copy V2 resample/DSP, UI, AutoSeq or CAT policy ownership.

### Component versions / approved driver patch

Resolved UAC **1.3.3**, CDC-ACM **2.2.0**, current IDF **v5.5.4**. Exact constraints
are in the ADV manifest. The generated dependency lock remains ignored according
to existing repository policy.

The architect approved the initial scope extension: reporting UAC 1.3.3's otherwise
silent native RX losses. `patch_uac_rx.py` checks the complete registry
`uac_host.c` SHA-256 against its published CHECKSUMS.json value:

```text
2d7549c7e4657744b92079c2c226b558e1934e587ab5030d80974f392eedf75e
```

It then checks the exact RX callback hash and adds TRANSFER_ERROR notification for:

- native ring overflow that drops a transfer;
- failed individual ISO packets;
- failed native ring pushes;
- failed transfer resubmission.

ADV CMake generates `build/t017_uac/uac_host.c` and substitutes that source in the
existing UAC component target. Managed source is unchanged. Source/hash/anchor
changes fail configuration instead of silently omitting the patch. Outside this
callback the generated source is byte-identical; the patch changes notifications
only, retaining packet movement, negotiation, TX and cancellation behavior.
A small attributed upstream callback fixture exercises the patch without requiring
managed dependencies in a Linux checkout. No complete component is vendored.

### USB host/UAC lifecycle

Host event task priority 5, UAC class task 5, capture worker 4, CDC driver 4 and CDC
owner 3 all outrank the foreground application (1). Open installs infrastructure
without requiring QMX. Start enables capture; reads return NOT_READY before first
connection. Only the QMX VID/PID and strict 48000/24/2 stream are accepted.
Non-UAC endpoints/handles delegate to the saved WAV callbacks. Backend handle
0x554143 is distinct from WAV's handle 1; public service handle ownership is
unchanged.

Stop joins capture/CDC owners, stops/closes UAC, closes CDC, uninstalls both class
drivers, drains host events and uninstalls the host PHY. Teardown failure returns
IO and retains reservation/resources for retry. Start re-prepares after a stop;
a later open retries cleanup left by failed app teardown. Enumeration/replug/MSC
reuse still need hardware validation.

### CDC companion result

CDC builds alongside UAC and is best effort. A separate owner attempts QMX interface
0, closes on disconnect/shutdown and logs readiness. Failure does not gate capture.
No line coding, DTR or CAT commands are sent. No descriptor fallback is added.
Runtime enumeration/CDC interoperability remains pending hardware testing.

### Canonical conversion / continuous capture / discontinuity

Private helper converts packed S24 LE to signed S16 by dropping the low byte,
equivalent to arithmetic shift by 8 without implementation-defined signed shifts.
Partial native frames and factor-four decimation phase persist across byte chunks.
Phase-zero selection matches Linux; channel order remains L/R. RxFrontend retains
mono/6k conversion and all shared FT8 code remains unchanged.

The producer drains UAC into a static 16384-frame canonical ring (65536 bytes),
independent of synchronous foreground decode. Critical sections protect producer,
consumer and callback state. Epoch tickets discard reads started while pending or
before a newer loss. Every reported loss republishes pending. Consumer ACK requires
native stop/start (including the driver's ring flush) before accepting a fresh
epoch. A second error during an in-flight read remains observable after the first
ACK. Canonical overflow flushes instead of overwriting unread frames.

WAIT_NONE performs no deliberate wait. Finite reads use a monotonic deadline and
one-tick waits while connected; read batches are capped at 256 canonical frames to
bound lock duration. Discontinuity returns zero frames. Native read timeout is
benign; other read failures publish loss. Transfer/read errors, high-water,
canonical overflows and loss counts are logged at stop.

### ADV default endpoint / preserved behavior

The ADV static wrapper injects `--rx uac:qmx` only when no explicit `--rx` exists,
using a bounded 32-entry argument array. Explicit WAV options pass through unchanged.
Linux defaults and shared MiniFT8 frontend/DSP/controller/UI are unchanged. Public
API, persisted data, ADV WAV implementation, speaker/Keyer, ELF and MSC source are
unchanged. There is no physical TX or CAT policy implementation.

### Files changed

- Root `CMakeLists.txt`: register the two new host regression tests.
- ADV `adv_audio_uac.cpp` and `adv_audio_uac_buffer.h`: lifecycle, native capture,
  canonical conversion, ring, epoch state, diagnostics and WAV delegation.
- ADV `adv_backend.c` / `adv_internal.h`: provider composition.
- ADV `main/CMakeLists.txt`, `main/idf_component.yml`, `main/ft8_static.c`:
  build dependencies, provider registration and packaged default endpoint.
- ADV `CMakeLists.txt` / `patch_uac_rx.py`: hash-guarded build-local driver patch.
- `tests/adv_audio_uac_buffer_test.c`: conversion/ring/epoch regressions.
- `tests/adv_uac_driver_patch_test.py` / `tests/fixtures/adv_uac_rx_1_3_3.c`:
  executable driver-notification regression and attributed upstream fixture.
- This task packet: authorized extension and implementation/test handoff.

### Tests run and results

Executed locally:

```bash
cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure
cmake -S tests/unit -B /tmp/T017-build-unit
cmake --build /tmp/T017-build-unit -j"$(nproc)"
ctest --test-dir /tmp/T017-build-unit --output-on-failure
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_dependency_boundary.py . ft8
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_platform_boundary.py . ft8
PYTHONDONTWRITEBYTECODE=1 python3 tests/ft8_platform_boundary.py .
source /home/wei/projects/esp-idf/export.sh
idf.py -C platform/adv build
git diff --check
```

Linux **39/39 PASS** (retains the 37-test baseline plus two T017 tests), portable
units **14/14 PASS**, architecture checks and checker self-tests **PASS**.
Conversion tests cover full-scale/sign boundaries including INT24 min/max and
negative fractions, independent channels, chunk lengths 1..29, partial frame/phase
continuity, partial reads, ring/counter wrap, full/no-overwrite, overflow flush,
pending/in-flight discard, second loss after ACK, reset gating and fresh delivery.
Driver test compiles and runs the patched upstream callback with ten deterministic
cases: normal/zero-byte packets, each of four newly reported loss paths, existing
whole-transfer error, cancellation, removal and inactive interface. It verifies
loss callbacks reach the canonical epoch and rejects modified/double-patched input.
The real build also exercises the complete-source hash guard and compiles the
generated driver (confirmed in the build log).

ADV firmware build **PASS**: binary **0xb7e50** bytes, app partition free
**0x5381b0** bytes (**88%**). Map comparison against the pre-T017 build:
`.dram0.bss` 0x2e80 -> 0x13018 (**+65944 bytes**), `.dram0.data` 0x4218 -> 0x4b68
(**+2384 bytes**). The fixed ring is 65536 bytes and DMA buffer 2304 bytes; component
and provider state account for additional static use. Runtime heap/largest-block
measurements require hardware; provider open logs these values. Whitespace check
passed. Lifecycle, dispatcher and packaged-default runtime validation remain
hardware/manual work; their build success is not a hardware acceptance claim.

### Hardware/manual validation still required

All three hardware phases above remain pending: USB console coexistence; QMX and
CDC enumeration; canonical capture; >=3 consecutive decoded slots; high-water and
no overflow during synchronous decode; repeated quit/relaunch with stable heap;
unplug/replug with discontinuity and recovered decoding; explicit ADV WAV RX;
speaker/Keyer/ELF regression and `usbmsc flash` after FT8 shutdown. Record actual
heap/largest block and ring diagnostics. Do not merge before live decode is confirmed.

### Known limitations / risks

16384 canonical frames cover approximately 1.365 seconds; decode occupancy has not
been measured on hardware. Static ring uses internal DRAM while idle, and USB/task
allocations add runtime heap demand. CDC uses interface 0 only. Native reset needs
real-device stop/start validation; transient low-level cleanup errors may retain
USB resources and require another cleanup attempt. Finite read latency is subject
to FreeRTOS tick granularity. The source hash deliberately requires patch review
when changing UAC versions. These are review/hardware follow-ups, not claims of
accepted runtime behavior. No other task-scope deviations were introduced.

### Commit reference

Single T017 implementation commit on `codex/T017-adv-usb-uac-rx`; exact pushed SHA
is returned in the Codex handoff. Status is REVIEW for supervisor diff review and
subsequent hardware testing. No PR and no GitHub Actions wait.

## Codex USB ownership amendment handoff

### Implementation summary

Implemented only the authorized USB-PHY/temporary-UART amendment, continuing the
existing branch. UAC preparation now obtains a private console lease: it calls
`adv_console_suspend_for_usb()`, prepares UART0 TX=GPIO4 / RX=GPIO5 at 115200,
then installs USB Host. The wiring matches `sdkconfig` at the pinned V2 reference
`491e757ae6b1e4cfd2b9a6ba10f48b35643849e0`; MiniShell's normal build-time console
selection remains USB Serial/JTAG.

The temporary UART uses 8N1, no flow control, 256-byte RX and 2048-byte TX buffers.
It requires no connected terminal for FT8 operation. GPIO5 is configured but has
no input consumer; Cardputer keyboard/display remain the only FT8 input/UI policy.

`esp_log_set_vprintf()` routes ESP-IDF/ADV logs to UART while the host lease is
active; the prior log sink is saved and restored. MiniShell System/debug output
through `adv_console_debug_write()` shares the UART route. The one direct ADV
filesystem error diagnostic now uses that route too; filesystem operations are
unchanged. Output and UART deletion are serialized with a recursive lock, including
callbacks already in flight when the log sink changes. The UART formatting buffer
is fixed at 512 bytes (long ESP log calls truncate to 511 output bytes).

### Failure unwind and preserved invariants

- The console lease records successful suspension and even partial UART setup.
  A failed USB suspension restores the existing VFS driver mode and does not claim
  the lease. UART setup failure releases only UART resources this owner acquired,
  then restores USB Serial/JTAG; it never deletes an already-owned foreign UART0.
- The provider joins capture/CDC workers, retries retained device closes,
  uninstalls CDC/UAC clients, drains host events and confirms successful
  `usb_host_uninstall()` before allowing console restoration. A device close error
  retains its handle instead of losing the resource needed for the next retry.
- If device/class/host teardown fails, the provider returns IO, retains its
  reservation/console lease and leaves UART diagnostics active. It does not restore
  USB Serial/JTAG over the owned PHY. Failed cleanup on a later open also returns
  IO. Existing cleanup retry paths remain available.
- Once Host and both class drivers are gone, cleanup drains/deletes the UART,
  restores its previous ESP log sink, releases GPIO4/5 and reinstalls Serial/JTAG.
  Failed UART cleanup or Serial/JTAG restoration retains the corresponding lease
  state for retry. Clean repeated entry/exit acquires/releases both owners anew.
- If failed cleanup returns to the local shell, that shell skips unavailable USB
  input. A retained UAC lease rejects a competing console suspension, preventing
  `usbmsc` from starting TinyUSB over a still-owned PHY. The MSC implementation and
  its normal handoff sequence are unchanged.
- No UART/USB logic entered MiniFT8 or public APIs. UAC format, converter, ring,
  discontinuity patch, WAV dispatch and packaged FT8 default are unchanged.

### Files changed

- `platform/adv/adv_console.c`: temporary UART, diagnostic routing, saved log sink,
  Serial/JTAG handoff/recovery and retained-lease protection.
- `platform/adv/adv_usb_console_handoff.h`: private, host-testable lease transitions.
- `platform/adv/adv_audio_uac.cpp`: acquire/release integration, retained device
  handles and diagnostics for cleanup errors.
- `platform/adv/adv_internal.h`: private console-handoff declarations.
- `platform/adv/adv_filesystem.c`: route its error diagnostic through ADV debug.
- `tests/adv_usb_console_handoff_test.c`, `tests/adv_usb_console_boundary.py`, root
  `CMakeLists.txt`: lifecycle/failure and integration regressions.
- This task report. No `adv_usbmsc.c`, shared FT8, SDK configuration, component
  version or managed-driver-patch change.

### Tests run and results

Re-ran the full local gate:

```bash
cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure
cmake -S tests/unit -B /tmp/T017-build-unit
cmake --build /tmp/T017-build-unit -j"$(nproc)"
ctest --test-dir /tmp/T017-build-unit --output-on-failure
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_dependency_boundary.py . ft8
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_platform_boundary.py . ft8
PYTHONDONTWRITEBYTECODE=1 python3 tests/ft8_platform_boundary.py .
source /home/wei/projects/esp-idf/export.sh
idf.py -C platform/adv build
git diff --check
```

Results: Linux **41/41 PASS**, portable units **14/14 PASS**, architecture checks
and checker self-tests **PASS**, real ESP-IDF **v5.5.4 ADV build PASS**,
whitespace check **PASS**. UAC 1.3.3 and CDC 2.2.0 remain resolved.

The new lease test injects suspend, UART setup, UART cleanup and console-restore
failures; verifies partial-prepare unwind, repeated teardown retries with a busy
PHY, no early UART shutdown/USB restoration, idempotent release and three complete
entry/exit cycles. The source regression connects those tested transitions to the
provider's actual prepare/release ordering, verifies the retained-host MSC guard,
GPIO4/5/115200 selection, saved/restored log sink and absence of UART input policy.
These are local software checks, not simulated claims of real QMX enumeration.

Firmware is **0xb8a20 bytes** with **0x5375e0 bytes (88%)** app partition free.
Compared with the initial T017 map, `.dram0.bss` increased from 0x13018 to 0x13030
(**24 bytes**); `.dram0.data` remains 0x4b68. UART driver buffers/locks add temporary
heap demand; actual free heap/largest block remains unmeasured.

### Hardware/manual validation still required / risks

**Hardware testing has not begun.** Await supervisor re-review before any flash,
serial terminal or QMX acceptance session. Still to validate: USB disconnect and
re-enumeration, GPIO4 diagnostics, repeated FT8 entry/quit, failure recovery, live
QMX decode/ring occupancy, heap stability, and the unchanged MSC sequence after
clean Host release. ESP log formatting is bounded and may truncate long diagnostic
calls; large diagnostic bursts can wait for UART TX capacity. ROM/panic output is
not reconfigured as a runtime UART console. UART input is intentionally unused.
The pre-existing UAC live-audio hardware acceptance also remains pending.

### Commit reference

New amendment commit on `codex/T017-adv-usb-uac-rx`, based on the existing
implementation and updated task packet. Its exact pushed SHA is returned in the
handoff. Status: REVIEW. No PR, no Actions wait, no hardware testing. No deviation
from the newly authorized amendment.

## Architect USB ownership decision

Accepted ownership model:

```text
M$> shell
    USB Serial/JTAG active
    usbmsc command available
        |
        +-- run ft8
        |      suspend/uninstall USB Serial/JTAG console
        |      take ESP32-S3 USB PHY for USB Host
        |      run UAC/CDC host while ft8 is active
        |      fully stop UAC/CDC + uninstall USB Host on ft8 exit
        |      restore USB Serial/JTAG console
        |      return to M$>
        |
        `-- run usbmsc
               existing device-mode handoff
               suspend USB Serial/JTAG
               use TinyUSB MSC temporarily
               restore USB Serial/JTAG on return
```

Rules:

- only one USB PHY owner at a time;
- `M$>` normally owns USB Serial/JTAG;
- `ft8` owns USB Host only while the live UAC endpoint is open/active;
- `usbmsc` keeps its existing temporary TinyUSB device-mode ownership;
- do not attempt UAC host and MSC simultaneously;
- local Cardputer display/keyboard remain available while USB Serial/JTAG is suspended;
- if USB Host teardown is incomplete, do not restore USB Serial/JTAG until the PHY is actually released;
- after clean FT8 exit, USB Serial/JTAG must return before control is considered back at the normal shell state.

## Architect FT8 debug-terminal decision

While `ft8` owns the ESP32-S3 USB PHY for USB Host, use the same temporary debug UART wiring as MiniFT8-V2:

```text
UART debug during ft8
TX = GPIO4
RX = GPIO5
baud = 115200
```

Ownership/state model:

```text
M$> normal shell
    USB Serial/JTAG console active
    GPIO4/5 debug UART inactive

enter ft8 with uac:qmx
    suspend/uninstall USB Serial/JTAG
    enable temporary GPIO4/5 UART debug terminal
    install USB Host
    run QMX UAC/CDC
    Cardputer display/keyboard remain the authoritative FT8 UI/input

exit ft8
    stop UAC/CDC
    uninstall USB Host and confirm PHY released
    disable temporary GPIO4/5 UART debug terminal
    restore USB Serial/JTAG console
    return to M$>
```

Scope/behavior:

- reuse the proven V2 UART0/custom-console wiring where practical;
- GPIO4/5 is a debug/diagnostic terminal during the FT8 USB-host window;
- preserve Cardputer display/keyboard as the normal local FT8 UI/input path;
- do not require GPIO4/5 UART for FT8 correctness;
- do not expose UART/ESP-IDF details to MiniFT8 application code;
- route ADV diagnostics that would otherwise disappear with USB Serial/JTAG to this temporary UART while host mode owns USB;
- if practical, permit RX input on GPIO5 for diagnostic shell/control only where ownership is unambiguous, but do not create a second competing FT8 UI policy;
- restore the pre-FT8 console state on every clean exit and on recoverable prepare failure;
- if USB Host teardown has not released the PHY, keep USB Serial/JTAG suspended, but the GPIO4/5 debug path may remain available to report the cleanup failure.

## Hardware finding — lazy 16384-frame ring still too large

The lazy-allocation correction successfully moved execution past the original FT8
workspace failure. On real ADV hardware, `Audio.open("uac:qmx")` now reports:

```text
ring allocation request bytes=65572 heap-free=96616 largest-block=39936
ring allocation failure bytes=65572 heap-free=96616 largest-block=39936
ft8: failed to start RX audio
app: ft8 returned 8
```

This confirms the FT8 workspace is now successfully allocated before Audio.open().
The remaining failure is specifically the UAC ring: a 65572-byte contiguous
allocation cannot fit in a 39936-byte largest block.

Architect decision for the next hardware iteration:

```text
ADV_UAC_RING_FRAMES = 2048
sample storage       = 8192 bytes
stream duration      = ~170.7 ms at 12 kHz
complete object      = ~8 KiB plus metadata
```

Rationale: T017 is a streaming path, so the ring should be sized from measured
producer/consumer backlog rather than used as bulk audio storage. However, MiniFT8 V3
decodes synchronously in the foreground, so the ring must absorb not only ordinary
USB/task jitter but also the interval during which decode temporarily prevents
foreground Audio.read() calls. Therefore do not jump directly to 2 KiB until real
decode high-water is known.

Required amendment:

1. Change the private ADV canonical ring capacity from 16384 to **2048 frames**.
2. Keep the lazy allocation model unchanged.
3. Update all capacity-dependent tests/log strings; no hard-coded `/16384` remains.
4. Prefer deriving the diagnostic capacity from `ADV_UAC_RING_FRAMES`.
5. Preserve conversion, epoch, overflow and discontinuity semantics unchanged.
6. Re-run Linux full CTest, units, architecture checks and real ADV build.
7. Record the exact runtime allocation size expected for the 2048-frame ring.
8. Return to hardware testing and capture:
   - ring allocation before/after heap + largest block;
   - USB Host/UAC/CDC startup;
   - high-water after at least three real FT8 decode slots;
   - overflow/discontinuity counts.
9. If high-water remains comfortably below capacity, later trim toward 1024 or
   512 frames (about 4 KiB / 2 KiB sample storage). If overflow occurs during
   synchronous decode, grow from measured need.

Do not change shared MiniFT8 or decode behavior for this memory adjustment.

## Codex 2048-frame capacity amendment handoff

### Implementation summary / files changed

Changed `ADV_UAC_RING_FRAMES` to **2048** in
`platform/adv/adv_audio_uac_buffer.h`. Sample storage is **8192 bytes**; the complete
`adv_uac_buffer_t` allocation is **8228 bytes**, including 36 bytes of metadata and
padding. This covers approximately 170.7 ms of canonical 12 kHz stereo audio.

`platform/adv/adv_audio_uac.cpp` now derives the high-water denominator from
`ADV_UAC_RING_FRAMES`. `tests/adv_uac_allocation_test.py` checks the approved capacity
and verifies the actual stop diagnostic reports the configured denominator. This
task report is the only other changed file.

### Behavior / invariants preserved

Lazy internal-heap allocation, allocation diagnostics, cleanup/retry ownership and
WAV delegation are unchanged. Conversion phase/channel order, ring wrap,
no-overwrite/overflow, epoch ACK and discontinuity behavior are unchanged. Existing
ring tests exercise full/overflow behavior at the new capacity using the constant.
Shared MiniFT8, console/USB lifecycle and usbmsc are unchanged. No deviations from
the requested capacity-only amendment.

### Tests run and results

```bash
cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure
cmake -S tests/unit -B /tmp/T017-build-unit
cmake --build /tmp/T017-build-unit -j"$(nproc)"
ctest --test-dir /tmp/T017-build-unit --output-on-failure
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_dependency_boundary.py . ft8
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_platform_boundary.py . ft8
PYTHONDONTWRITEBYTECODE=1 python3 tests/ft8_platform_boundary.py .
source /home/wei/projects/esp-idf/export.sh
idf.py -C platform/adv build
git diff --check
```

All passed: Linux **42/42**, units **14/14**, architecture checks/checker self-tests,
real IDF **v5.5.4 ADV build**, and whitespace check. Production source has no literal
old high-water denominator. Firmware is **0xb8aa0 bytes**, app partition free
**0x537560 bytes (88%)**. `.dram0.bss` remains **0x3010 (12304 bytes)** and
`.dram0.data` remains **0x4b68**; the ring is still allocated lazily.

### Hardware/manual validation still required / risks

**Hardware testing has not resumed.** Await supervisor review before the next run.
Then record allocation/heap diagnostics, USB startup, and high-water plus
overflow/discontinuity counts over at least three synchronous decode slots. The
smaller backlog budget has not been validated on hardware; grow or trim only with
measured evidence and authorization. Pending lifecycle/MSC acceptance remains.

### Commit reference

Bounded capacity amendment on `codex/T017-adv-usb-uac-rx`; exact pushed SHA returned
in the handoff. Status: REVIEW. No PR, no Actions wait, no hardware testing.

## Supervisor 2048-frame ring re-review

PASS for hardware testing on `21ca5a2e637f1658aefbea8490046e2bd9bf46e8`.

The change from the accepted lazy-ring implementation is bounded to capacity-dependent
code/tests/documentation. `ADV_UAC_RING_FRAMES` is 2048; the stop diagnostic derives
its denominator from that constant; lazy allocation, console handoff, USB lifecycle,
conversion and discontinuity semantics are unchanged. The lifecycle regression
explicitly asserts the approved 2048-frame capacity.

The complete ring object is approximately 8228 bytes (8192 bytes of stereo S16 sample
storage plus metadata/alignment), comfortably below the 39936-byte largest free block
measured after the FT8 workspace was allocated on hardware.

Linux CTest 42/42, units 14/14, architecture checks and the real ADV build are accepted.
T017 returns to TESTING. Resume the same hardware launch; record the allocation logs,
GPIO4 diagnostics, QMX enumeration and then ring high-water over real decode slots.

## Supervisor lazy-ring re-review

PASS for resumed hardware testing on `14bc505f4abb9c049b0772d005f6ffb67dcc702e`.

The lazy allocation amendment is accepted. The permanent UAC ring has been replaced
by a nullable provider-owned pointer; `rx_open("uac:qmx")` allocates and zeroes the
ring before any console/USB ownership handoff, and non-UAC/WAV endpoints bypass the
allocation entirely. Allocation failure returns NO_MEMORY while normal USB
Serial/JTAG is still active. Clean close frees the ring only after successful
class/Host/console cleanup; failed teardown retains both allocation and reservation
for retry.

The real ADV map confirms the intended result: .dram0.bss fell from 77872 bytes to
12304 bytes, recovering 65568 bytes. Linux CTest 42/42, units 14/14, architecture
checks and the real ADV build are accepted.

For the next hardware run, keep the current 16384-frame (~64 KiB) runtime ring.
Its size is not an architectural requirement: record ring high-water during real
synchronous FT8 decode, then trim later from measured need if RAM pressure warrants.

Resume the exact launch that previously returned 8. First success criterion is that
FT8 now reaches the UAC allocation/console handoff and GPIO4 diagnostics; only then
continue to QMX enumeration/live-decode acceptance.

## Supervisor review

PASS for hardware testing on `2f7f50937326954a151da7bdcff342027a312744`.

The original UAC implementation plus the USB ownership amendment were reviewed against
current `main`. The amended ordering is accepted:

```text
USB Serial/JTAG suspend
-> temporary UART0 GPIO4/5 diagnostics
-> USB Host/UAC/CDC ownership
-> device/class cleanup
-> confirmed usb_host_uninstall()
-> temporary UART cleanup
-> USB Serial/JTAG restore
```

Partial prepare failures retain the console lease until cleanup completes. Teardown
failures do not restore USB Serial/JTAG while the USB host/classes are still owned.
The private handoff helper tests repeated entry/exit and suspend/UART-cleanup/restore
failures; the source-boundary test ties those transitions to the production
prepare/release order. Shared MiniFT8, usbmsc, public APIs and FT8 DSP remain unchanged.

Linux CTest 41/41, units 14/14, architecture checks and the real ADV build are accepted.
The first hardware run exposed a pre-UAC memory-allocation failure. T017 is reopened
for the architect-approved lazy-ring amendment above. Do not resume hardware testing
until that amendment is implemented and re-reviewed.

The prior implementation remains otherwise acceptable in structure: UAC/CDC stay
platform-private; canonical conversion/ring/discontinuity ownership is correct;
WAV delegation and ADV ft8 packaging are preserved. The required delta is the
explicit console/PHY handoff plus temporary V2-style GPIO4/5 debug UART described
above.

The implementation is otherwise well-shaped: UAC/CDC remain platform-private, the provider emits canonical 12 kHz/S16/stereo, the 16K-frame ring/epoch logic preserves continuity facts, WAV delegation is retained, bare ADV ft8 injects uac:qmx only at composition, Linux 39/39 and units 14/14 pass, and the real ADV build succeeds.

However, current MiniShell ADV boot installs the ESP32-S3 USB Serial/JTAG driver in adv_console_prepare(). T017 then calls usb_host_install() without first releasing that console. Espressif documents that ESP32-S3 USB-OTG and USB-Serial/JTAG share one internal PHY and only one can operate at a time without an external PHY. The Cardputer ADV documentation does not identify an external USB PHY, and the pinned MiniFT8-V2 configuration avoided this conflict by using a custom UART console (GPIO4/5), not USB Serial/JTAG.

Required amendment:
1. Before usb_host_install(), call the existing adv_console_suspend_for_usb() and record provider ownership of that suspension.
2. If preparation fails after suspension, restore the console during cleanup once USB host ownership is gone.
3. During normal stop/close, restore the console only after UAC/CDC clients are uninstalled and usb_host_uninstall() has actually released the PHY.
4. If USB host teardown is incomplete, do not re-enable USB Serial/JTAG over a still-owned PHY; retain the suspended state and return IO so the existing retry path can finish cleanup later.
5. Repeated ft8 -> quit -> ft8 must suspend/resume cleanly each time.
6. Keep usbmsc's existing handoff behavior unchanged; do not attempt simultaneous UAC-host and device-mode MSC.
7. Add a narrow state-machine/unit/source regression if practical so prepare/release cannot accidentally resume the console before host release.
8. Re-run Linux 39/39, units 14/14, architecture checks, and the real ADV build.

After this amendment, return a new implementation SHA for supervisor re-review. Do not begin hardware acceptance on 454368fe570e1f2df2efa1505242c50a0f0c4b6d.

## Architect memory-allocation amendment

Hardware measurement immediately before launching FT8:

```text
app used      0 B
heap free     264.5 KiB
largest block 208.0 KiB
```

This confirms the first hardware failure is a pre-UAC memory-pressure problem.
The production FT8 monitor baseline requires about 206 KiB contiguous, while T017
currently reserves the 16384-frame UAC ring (65536 bytes) permanently in static
internal DRAM. `app_controller_start_rx()` allocates AppRxState and the FT8 engine
workspace before calling Audio.open(), so the 208 KiB shell-state largest block is
too close to the FT8 workspace requirement to survive those preceding allocations.

Required amendment:

1. Remove the permanent static `adv_uac_buffer_t ring` allocation from .bss.
2. Keep only a nullable provider-owned pointer/state in permanent storage.
3. In `rx_open("uac:qmx")`, after the application/FT8 workspace already exists
   but **before** `adv_console_begin_usb_host()`, allocate one
   `adv_uac_buffer_t` dynamically from internal 8-bit-capable heap.
4. Log before/after allocation while USB Serial/JTAG is still active:
   - requested bytes;
   - heap free;
   - largest block;
   - allocation success/failure.
5. Zero/init the allocated ring exactly as the former static object was initialized.
6. Pass/use the pointer through all conversion/ring/epoch paths without changing
   public Audio semantics.
7. On clean UAC close after USB/class/console teardown is complete, free the ring
   and clear the pointer.
8. If teardown fails and provider reservation/cleanup obligation is retained, do
   **not** free the ring prematurely; retain it until cleanup succeeds.
9. On prepare failure after ring allocation:
   - unwind USB/console ownership as already specified;
   - once ownership cleanup succeeds, free the ring and clear reservation;
   - if ownership cleanup fails, retain ring + reservation for retry.
10. An Audio-open ring allocation failure must:
    - occur before USB Serial/JTAG suspension;
    - print a diagnostic on the normal USB console;
    - return MINI_ERR_NO_MEMORY or the nearest existing appropriate backend result;
    - leave USB ownership untouched.
11. WAV and other non-UAC endpoints must allocate no UAC ring.
12. Keep the architect-reconfirmed 16384-frame ring. Restore the intended allocation
    order first; measure real ring high-water during synchronous decode before any
    separately authorized capacity change.
13. Add/adjust host tests for:
    - lazy allocation state;
    - allocation failure with no USB/console handoff;
    - cleanup success frees ring;
    - cleanup failure retains ring;
    - later cleanup retry frees it;
    - repeated open/close does not leak.
14. Re-run Linux full CTest, unit suite, architecture checks and real ADV build.
15. Record new firmware .bss size; the expected result is approximately 64 KiB
    recovered from static .bss relative to the current T017 build. The runtime
    allocation remains about 64 KiB, now acquired after the FT8 workspace.

Expected runtime allocation order:

```text
M$> shell
    no UAC ring allocated

ft8 startup
    AppController / AppRxState
    FT8 monitor workspace (~206 KiB contiguous)
    Audio.open("uac:qmx")
        allocate 64 KiB canonical UAC ring
        then suspend USB Serial/JTAG
        then install USB Host/UAC/CDC

ft8 clean exit
    teardown UAC/CDC/USB Host
    restore USB Serial/JTAG
    free UAC ring
    return M$>
```

After this amendment, repeat the exact hardware launch that previously returned 8.
Only if FT8 reaches the GPIO4 handoff should subsequent USB enumeration debugging begin.

## Codex lazy-ring amendment handoff

### Implementation summary / files changed

Implemented only the architect memory-allocation amendment. The provider keeps a
nullable `adv_uac_buffer_t *ring` instead of the permanent static object. Only
`rx_open("uac:qmx")` allocates it, using
`heap_caps_malloc(sizeof(*ring), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)`. Allocation
and zero-initialization precede `prepare()` and therefore precede all USB console
suspension, UART setup and USB Host ownership.

Changed files: `platform/adv/adv_audio_uac.cpp`, new
`tests/adv_uac_allocation_test.py`, root `CMakeLists.txt` (test registration), and
this task report. The converter/ring helper, UART/console implementation, managed
UAC patch, USB/class teardown ordering, shared MiniFT8, public API and usbmsc are
unchanged.

### Behavior / invariants preserved

The 16384-frame capacity is unchanged: 65536 sample-storage bytes, **65572 bytes**
for the complete object including metadata. Before/after allocation logs report
requested bytes, internal 8-bit heap free and largest block, and success/failure
while normal USB Serial/JTAG remains active. Allocation failure leaves an invalid
output handle, clears the reservation, returns `MINI_ERR_NO_MEMORY`, and never
enters USB/console prepare.

All conversion, capture, loss/ACK, read and statistics paths use the pointer.
The ring stays allocated across stop/start to preserve existing epoch behavior and
the validity of an open handle. Stop still performs its existing hardware teardown;
only successful close frees the ring after complete class/Host/console cleanup.
This also keeps post-stop statistics safe to read. Failed close retains ring and
reservation for retry.

If open's prepare fails, successful ownership unwind frees the ring and clears the
reservation. If unwind fails, both remain. A later open must complete the retained
cleanup before freeing the old ring and allocating a freshly zeroed one. Non-UAC
endpoint delegation occurs before any UAC reservation/allocation; WAV paths
allocate no UAC ring. No permanent native-buffer or ring-capacity tuning was made.

### Tests run and results

```bash
PYTHONDONTWRITEBYTECODE=1 python3 tests/adv_uac_allocation_test.py
cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure
cmake -S tests/unit -B /tmp/T017-build-unit
cmake --build /tmp/T017-build-unit -j"$(nproc)"
ctest --test-dir /tmp/T017-build-unit --output-on-failure
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_dependency_boundary.py . ft8
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_platform_boundary.py . ft8
PYTHONDONTWRITEBYTECODE=1 python3 tests/ft8_platform_boundary.py .
source /home/wei/projects/esp-idf/export.sh
idf.py -C platform/adv build
git diff --check
```

All passed: Linux **42/42**, portable units **14/14**, architecture checks and
checker self-tests, real IDF **v5.5.4 ADV firmware build**, and whitespace check.

The new host regression compiles the production allocation/free and
open/start/stop/close functions against injected heap/logging and prepare/release
operations. It covers initially unallocated state; WAV/default/other endpoint
bypass; unsupported format; allocation failure before handoff with diagnostics;
zero-initialization; three leak-free open/close cycles; stop/start retaining the
same ring and pending epoch; prepare failure with successful/failed unwind;
failed cleanup retries retaining the same allocation; eventual retry cleanup/free;
and close failure followed by successful cleanup. The existing console-boundary
regression continues to check actual class/Host/console release ordering.

### Firmware memory result

From `platform/adv/build/minishell_adv.map`:

| Section | Previous T017 | Lazy ring | Change |
| --- | ---: | ---: | ---: |
| `.dram0.bss` | 0x13030 (77872 B) | **0x3010 (12304 B)** | **-65568 B** |
| `.dram0.data` | 0x4b68 | 0x4b68 | unchanged |

The map now shows the provider ring symbol occupying **4 bytes**, a pointer. The
recovered static storage equals the 65572-byte object minus that pointer. Firmware
binary size is **0xb8bc0 bytes**, with **0x537440 bytes (88%)** of the app partition
free. This proves the permanent ring cost is removed; it does not measure runtime
heap fragmentation or prove the FT8 startup failure resolved on hardware.

### Hardware/manual validation still required / known risks

**Hardware testing has not resumed.** Await supervisor re-review, then repeat the
architect's exact failed launch and record shell heap/largest block plus the new
allocation logs before proceeding to UART/USB enumeration. Internal heap must still
accommodate the 65572-byte ring after the FT8 workspace and before USB/UART/task
allocations. Capacity remains 16384 until real decode high-water measurements
justify a separate change. All pending live decode/teardown/MSC hardware acceptance
remains pending. The host regression injects USB cleanup outcomes; it does not
exercise ESP-IDF or real-device teardown.

### Commit reference

New bounded memory-allocation amendment commit on `codex/T017-adv-usb-uac-rx`;
exact pushed SHA returned in the Codex handoff. Status: REVIEW. No PR, no Actions
wait, no resumed hardware testing, and no deviations from the amendment.

## Hardware finding — first T017 run

First real ADV run failed before USB-host ownership:

```text
ft8: failed to start RX audio
app: ft8 returned 8
GPIO4 debug UART: no output
```

Interpretation: result 8 covers `app_controller_start_rx()`, and that function allocates
the FT8 engine/workspace before calling the Audio provider's `open()`. Therefore no
GPIO4 banner strongly indicates execution never reached `adv_console_begin_usb_host()`
or `usb_host_install()`.

Memory pressure is the primary hypothesis. The production FT8 baseline
time_osr=2/freq_osr=2 monitor requires approximately 206 KiB of one contiguous
workspace (waterfall alone is 161076 bytes). T017 added approximately 64 KiB static
DRAM for the 16384-frame canonical UAC ring before FT8 starts. The pre-T017 ADV
largest-block baseline was about 280 KiB, so the static ring plus foreground app stack
can plausibly make the FT8 workspace allocation fail before Audio open.

Required confirmation before changing USB code:

```text
M$> free
```

Record free heap and largest block immediately before launching FT8.

Preferred correction if the hardware numbers confirm this diagnosis:

- do not keep the 16384-frame UAC canonical ring as permanent static .bss;
- allocate the provider ring lazily only for the `uac:qmx` endpoint, after the FT8
  engine workspace has already been allocated;
- free the ring on clean Audio close;
- retain it if teardown fails and cleanup obligation remains;
- retain the architect-reconfirmed 16384 canonical frames / 65536 sample bytes;
- measure high-water during synchronous decode before any authorized size change;
- WAV/shell/non-UAC operation must pay no 64 KiB UAC ring cost;
- preserve all conversion/epoch/discontinuity semantics and existing host tests;
- add allocation failure coverage and diagnostics showing requested ring bytes plus
  free/largest heap around UAC open;
- after the correction, repeat the same hardware test before investigating USB PHY
  or enumeration.

Do not treat the absence of GPIO4 output as evidence of a USB-host failure until the
pre-Audio FT8 allocation path is ruled out.

## Hardware finding — USB handoff succeeds; input responsiveness under test

Real ADV result with the 2048-frame ring:

```text
M$> ft8
I (...) adv_uac: ring allocation request bytes=8228 heap-free=96616 largest-block=39936
I (...) adv_uac: ring allocation success bytes=8228 heap-free=88164 largest-block=32256

FATAL: read zero bytes from port
```

Interpretation:

- the FT8 workspace and lazy UAC ring now both allocate successfully;
- the USB Serial/JTAG monitor disconnect is expected and is evidence that the
  console handoff reached USB-host ownership;
- USB Serial/JTAG cannot be used as the FT8 terminal while host mode owns the PHY;
- GPIO4 diagnostics require a separate UART adapter on GPIO4 TX + GND.

After QMX is connected, FT8 becomes visible/running but the reported key response is
unclear whether it is only the now-disconnected PC terminal or also the physical
Cardputer keyboard. Before changing scheduling, isolate with:

```text
1. start ft8 with QMX disconnected;
2. after USB Serial/JTAG disconnects, press Q on the physical Cardputer keyboard;
3. observe whether FT8 exits / USB Serial/JTAG returns;
4. repeat, then connect QMX and immediately test physical Q again;
5. note whether the top UTC/slot counter continues advancing after QMX connects.
```

If physical keyboard and top-line updates work before QMX but freeze only after QMX
streaming begins, investigate scheduling/starvation. MiniShell foreground applications
are pinned to core 0 at priority 1. Current T017 has UAC background work at higher
priorities, including UAC driver core 0 priority 5, CDC driver core 0 priority 4,
and capture priority 4. The pinned V2 reference placed the audio stream task on core 1.
A likely follow-up is to isolate USB/UAC capture work from the MiniShell foreground
core and/or ensure the successful capture loop yields, but do not make that change
until this hardware discriminator is recorded.

## Supervisor control-transfer re-review

PASS for hardware testing on `bc35ff129954ce107f2b99fedaba2c42c9fdab2c`.

The amendment is config-only and bounded:
- `platform/adv/sdkconfig.defaults` now sets
  `CONFIG_USB_HOST_CONTROL_TRANSFER_MAX_SIZE=2048`;
- `adv_config_guard.c` fails closed below 2048;
- no UAC runtime, scheduling, buffering, MiniFT8, console-handoff or usbmsc behavior changed.

Linux CTest 42/42, units 14/14, architecture checks, guard checks and the real ADV
rebuild are accepted. T017 returns to TESTING.

Next hardware success criterion: with GPIO4 diagnostics active, QMX enumeration must
proceed beyond the previous `CHECK_SHORT_CONFIG_DESC FAILED` point and reach UAC RX
connected/open plus strict 48000/24/2 stream start.

## Hardware finding — QMX enumeration blocked by control-transfer limit

Connecting QMX after FT8/USB Host startup produces:

```text
E (...) ENUM: Configuration descriptor larger than control transfer max length
E (...) ENUM: [0:0] CHECK_SHORT_CONFIG_DESC FAILED
```

This supersedes the earlier scheduler/freeze hypothesis for the current failure.
QMX is a composite USB device, and enumeration stops before class/device startup
because the host control-transfer buffer is too small for its full configuration
descriptor.

At the failing hardware build, MiniShell ADV `sdkconfig.defaults` did not set
`CONFIG_USB_HOST_CONTROL_TRANSFER_MAX_SIZE`; ESP-IDF therefore used its small
default. The pinned MiniFT8-V2 reference explicitly used:

```text
CONFIG_USB_HOST_CONTROL_TRANSFER_MAX_SIZE=2048
```

Architect decision:

1. Add `CONFIG_USB_HOST_CONTROL_TRANSFER_MAX_SIZE=2048` to
   `platform/adv/sdkconfig.defaults`.
2. Add an ADV compile-time config guard requiring the value to be at least 2048,
   so regenerating sdkconfig cannot silently regress QMX enumeration.
3. Do not change task priorities/affinities for this issue.
4. Do not add the previously proposed breadcrumb instrumentation unless another
   failure remains after QMX enumeration succeeds.
5. Rebuild the real ADV firmware from the updated defaults/config.
6. Hardware test with GPIO4 debug:
   - USB Host install;
   - QMX enumeration completes without CHECK_SHORT_CONFIG_DESC;
   - UAC RX connected/opened;
   - 48000/24/2 starts;
   - CDC status;
   - Cardputer UI/key responsiveness.
7. If a responsiveness issue remains after successful enumeration/stream start,
   diagnose it separately with evidence.

Memory note: this raises the per-device default-control transfer buffer from the
ESP-IDF default to 2048 bytes. For the single QMX device this is a small runtime
cost compared with the already measured FT8/UAC memory budget.

## Codex QMX enumeration configuration handoff

### Implementation summary / files changed

Added `CONFIG_USB_HOST_CONTROL_TRANSFER_MAX_SIZE=2048` to
`platform/adv/sdkconfig.defaults`, matching the pinned MiniFT8-V2 `sdkconfig` at
`491e757ae6b1e4cfd2b9a6ba10f48b35643849e0`. Added a guard in the already-built
`platform/adv/adv_config_guard.c` that rejects missing values or values below 2048
with an explicit QMX/config-regeneration error. This report is the only other
tracked change.

### Behavior / invariants preserved

Only the ADV USB Host control-transfer limit changes. Task priorities, core
affinities, shared MiniFT8, UAC ring/allocation, console handoff and USB lifecycle
are unchanged. The superseded freeze breadcrumbs were not implemented. No task
scope deviations.

### Configuration and local validation

The existing ignored `platform/adv/sdkconfig` still contained an explicit 256-byte
value, which would override a new default. Saved it to
`/tmp/T017-enum-sdkconfig-before`, removed only that assignment, then ran
`idf.py -C platform/adv reconfigure build`. Verified the regenerated sdkconfig is
byte-identical to the saved copy except for **256 -> 2048**. Both sdkconfig and
`build/config/sdkconfig.h` resolve to 2048. Generated configuration remains ignored;
only defaults and the guard are committed.

Exact local build/test gate:

```bash
cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure
cmake -S tests/unit -B /tmp/T017-build-unit
cmake --build /tmp/T017-build-unit -j"$(nproc)"
ctest --test-dir /tmp/T017-build-unit --output-on-failure
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_dependency_boundary.py . ft8
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_platform_boundary.py . ft8
PYTHONDONTWRITEBYTECODE=1 python3 tests/ft8_platform_boundary.py .
source /home/wei/projects/esp-idf/export.sh
idf.py -C platform/adv reconfigure build
git diff --check
```

Results: Linux **42/42 PASS**, units **14/14 PASS**, architecture checks/checker
self-tests **PASS**, real IDF **v5.5.4 ADV rebuild PASS**, whitespace check **PASS**.
Also compiled the actual guard with `cc -fsyntax-only -I <temporary-config-dir>
platform/adv/adv_config_guard.c`, using copies of the generated header with only
this option varied: **undefined, 256, 2047 rejected** with the new diagnostic;
**2048 and 4096 accepted**. No test-only configuration was used for firmware.

Firmware size remains **0xb8aa0 bytes**, app partition free **0x537560 bytes (88%)**.
`.dram0.bss` remains **0x3010**, `.dram0.data` remains **0x4b68**. The larger
per-device control-transfer buffer has a runtime heap cost, not a new static ring.

### Hardware/manual validation still required / risks

**Hardware testing has not resumed.** Await supervisor review before checking QMX
configuration-descriptor enumeration, UAC interface open and 48000/24/2 startup,
CDC status, Cardputer responsiveness and the remaining live decode/lifecycle tests.
The local build proves the intended limit is effective; successful real-device
enumeration and runtime heap headroom remain unverified. Any residual freeze after
enumeration must be investigated separately from measured evidence.

### Commit reference

Bounded enumeration-config commit on `codex/T017-adv-usb-uac-rx`; exact pushed SHA
returned in the handoff. Status: REVIEW. No PR, no Actions wait, no hardware testing.

## Hardware finding — no-QMX freeze after UAC install

With QMX disconnected, GPIO4 shows:

```text
ADV: USB Host diagnostics on UART0 TX=GPIO4 RX=GPIO5 115200
I (...) adv_uac: USB Host installed FIFO 91/18/91; heap 78908 largest 31744
I (...) uac-host: Install Succeed, Version: 1.3.3
```

After the UAC install message the ADV appears frozen: the physical Cardputer keyboard
does not respond. Because no QMX is present, this occurs before UAC device
enumeration or audio streaming. Do not attribute it to capture-ring backlog or QMX
traffic.

SUPERSEDED by the configuration-descriptor finding above. Do not implement the
following breadcrumb plan unless a later post-enumeration failure requires it. The
previous proposed breadcrumbs were:

```text
ADV_UAC prepare: CDC driver install begin/result
ADV_UAC prepare: CDC owner task create result
ADV_UAC prepare: UAC driver install begin/result
ADV_UAC prepare: capture task create result
ADV_UAC prepare: complete
ADV_UAC start: entered/complete
ADV_UAC read: first call
ADV_UAC read: first NOT_READY/other result
```

Requirements:

1. Keep breadcrumbs in `platform/adv/adv_audio_uac.cpp`; do not modify shared
   MiniFT8 merely for diagnostics.
2. Each transition should log once or be rate-limited; do not flood the UART.
3. Include task-create return status and heap/largest block around any failed create.
4. Preserve current task priorities/affinities for this diagnostic build.
5. Do not change UAC/CDC ownership or behavior yet.
6. Re-run the normal software/build gates.
7. Return to hardware with QMX disconnected and record the last breadcrumb reached.
8. Only after that evidence should T017 change task affinity/priorities.

Scheduling remains a plausible later hypothesis: the MiniShell foreground app is
pinned to core 0 priority 1, while UAC/CDC driver tasks are currently higher priority
and pinned to core 0. The pinned V2 reference used core 0 for USB/UAC class tasks
but moved the continuous FT8 audio stream task to core 1. However, because this
freeze occurs before a QMX stream exists, evidence should identify the exact
transition before changing the topology.

## Hardware finding — host/UART bring-up without QMX passes

Real ADV test with QMX disconnected reached the expected host-wait state:

```text
ADV: USB Host diagnostics on UART0 TX=GPIO4 RX=GPIO5 115200
I (...) adv_uac: USB Host installed FIFO 91/18/91; heap 78908 largest 31744
I (...) uac-host: Install Succeed, Version: 1.3.3
```

This confirms:
- lazy ring allocation has already succeeded;
- USB Serial/JTAG -> GPIO4/5 diagnostic handoff works;
- USB Host installation works;
- UAC host 1.3.3 installation works with no device attached;
- the remaining responsiveness issue is downstream of this point.

Next discriminator: while FT8 is waiting with QMX disconnected, press physical Cardputer
`Q`. If FT8 exits and USB Serial/JTAG returns, the local UI/input path is healthy
before QMX enumeration and the later freeze is QMX-triggered.

## Architect hardware result

Record QMX enumeration, live decoded messages, consecutive-slot behavior, repeated
lifecycle, CDC status, ring high-water/continuity, and usbmsc-after-FT8 result here.

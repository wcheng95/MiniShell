# T017 — ADV QMX USB-host UAC RX vertical slice

Status: REVIEW

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

Initial target:

```text
16384 canonical frames
= 65536 bytes
= about 1.365 seconds at 12 kHz
```

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

## USB console and usbmsc coexistence

The existing ADV USB Serial/JTAG console must remain usable according to the current
board behavior.

The OTG host lifecycle must be fully released when the UAC stream closes so the
existing TinyUSB device-mode MSC workflow still works afterward.

Hardware acceptance therefore includes:

```text
ft8 live UAC
-> quit to M$>
-> usbmsc flash
-> PC sees storage
-> eject
-> Q returns to M$>
```

Do not attempt simultaneous UAC-host and MSC-device mode.

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

The architect approved the sole scope extension: reporting UAC 1.3.3's otherwise
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

## Supervisor review

Supervisor reviews platform ownership, buffering, lifecycle, default endpoint packaging,
and absence of V2 application coupling before hardware testing.

## Architect hardware result

Record QMX enumeration, live decoded messages, consecutive-slot behavior, repeated
lifecycle, CDC status, ring high-water/continuity, and usbmsc-after-FT8 result here.

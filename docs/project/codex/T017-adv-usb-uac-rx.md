# T017 — ADV QMX USB-host UAC RX vertical slice

Status: READY

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

Mandatory T017 completion:

- [ ] V2 USB host/UAC mechanics traced and adapted, not blindly copied;
- [ ] QMX UAC endpoint `uac:qmx` exists on ADV;
- [ ] strict 48k/24-bit/stereo negotiation works;
- [ ] native UAC audio becomes MiniShell 12k/S16/stereo;
- [ ] channel order preserved;
- [ ] existing RxFrontend remains owner of mono/6k conversion;
- [ ] continuous producer drains UAC during synchronous decode;
- [ ] ring overflow/USB loss becomes explicit discontinuity;
- [ ] ADV WAV RX remains usable;
- [ ] bare ADV `ft8` defaults to live `uac:qmx`;
- [ ] explicit `--rx` overrides default;
- [ ] live decoded FT8 messages appear on ADV RX screen for >=3 consecutive slots;
- [ ] FT8 can exit/re-enter repeatedly;
- [ ] USB host teardown permits subsequent `usbmsc`;
- [ ] Linux 37/37 baseline remains green;
- [ ] portable unit suite remains green;
- [ ] ADV firmware builds;
- [ ] no public API expansion;
- [ ] no FT8 platform dependency;
- [ ] no physical TX implementation;
- [ ] no unrelated cleanup.

CDC best-effort acceptance:

- [ ] CDC component installs alongside UAC if compatible;
- [ ] QMX CDC interface opens and disconnects cleanly;
- [ ] CDC failure does not break UAC RX;
- [ ] no CAT policy commands are sent in T017.

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

### V2 reference mapping

### Component versions / API compatibility

### USB host/UAC lifecycle

### CDC companion result

### Canonical audio conversion

### Continuous capture / ring / discontinuity

### ADV default FT8 endpoint

### Tests added

### Files changed

### Local Linux/unit/ADV build results

### Hardware validation still required

### Known limitations / risks

### Commit

## Supervisor review

Supervisor reviews platform ownership, buffering, lifecycle, default endpoint packaging,
and absence of V2 application coupling before hardware testing.

## Architect hardware result

Record QMX enumeration, live decoded messages, consecutive-slot behavior, repeated
lifecycle, CDC status, ring high-water/continuity, and usbmsc-after-FT8 result here.

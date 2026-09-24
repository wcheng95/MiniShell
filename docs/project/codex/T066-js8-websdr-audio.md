# T066 — JS8 Linux WebSDR/browser audio input

Status: COMPLETE

## Architect intent

Use a remote WebSDR as the JS8 receive front end when the local antenna is poor.

The first target is KFS WebSDR on pc-1/Linux. The browser remains the WebSDR client:
it tunes/demodulates the remote receiver and produces ordinary desktop audio. MiniShell
must capture that decoded browser audio and feed the existing JS8 live receive path.

This is the Linux equivalent of using a virtual audio cable on Windows. Do **not**
reverse-engineer or embed the KFS/WebSDR network/audio protocol in JS8Chat.

## Objective

Add a Linux MiniShell Audio endpoint for PulseAudio/PipeWire monitor sources and
add bounded receive-latency correction to the existing JS8 live scheduler.

Target operator flow:

```text
browser: KFS WebSDR -> tune 7.078 MHz USB -> Audio Start

M$> js8chat --rx pulse:@DEFAULT_MONITOR@ \
             --rx-delay-ms 800 \
             --dial-hz 7078000 \
             --log /flash/js8chat/activity.jsonl
```

The exact delay is source/browser dependent and is operator-selected. The example
800 ms is not a default or calibrated KFS value.

`--dial-hz` remains logging/RF metadata unless local CAT is explicitly supplied.
For WebSDR operation do not use local QMX CAT.

## Why browser audio, not WebSDR protocol integration

KFS currently uses the classic PA3FWM WebSDR family and browser WebAudio. That
wire protocol is server-specific and has changed over time. It is outside the JS8
application architecture and unnecessary here.

Linux desktop audio already exposes playback sink monitor sources through
PulseAudio/PipeWire. Capture that PCM through the platform Audio provider, leaving:

```text
KFS / other WebSDR
    -> browser WebAudio
    -> PulseAudio/PipeWire sink monitor
    -> MiniShell Linux Audio
    -> existing 12 kHz S16 stereo contract
    -> existing JS8 frontend / scheduler / decoder / logger
```

This source should also be reusable by other MiniShell receive applications later;
do not make the platform endpoint JS8-specific.

## Current baseline

T064 is COMPLETE and provides the accepted Linux/QMX live JS8 RX path:

```text
js8chat --rx alsa:<QMX> [--dial-hz hz] [--cat serial:<QMX>] [--log path]
```

The app requests exactly:

```text
12000 Hz
S16
stereo
```

from MiniShell Audio. The Linux QMX provider currently opens the hardware at
48 kHz / S24_3LE / stereo and phase-decimates to that application contract.

The JS8 live scheduler uses MiniFT8-style pre-roll:

```text
target slot S
capture starts: S - 1.6 s
capture length: 89280 @ 6 kHz = 14.88 s
waterfall: 93 blocks
```

Each chunk is currently anchored to local MiniShell UTC. A browser/network source
may arrive hundreds of milliseconds or more after RF time, so source delay must be
removed before slot scheduling.

## Source of truth

```text
AGENTS.md
include/minishell/api.h
platform/linux/linux_audio_wav.c
apps/js8chat/README.md
apps/js8chat/main/js8chat_main.c
apps/js8chat/src/live_rx/js8_live.c
apps/js8chat/src/live_rx/js8_live.h
apps/js8chat/src/live_rx/js8_slot.c
apps/js8chat/src/live_rx/js8_timing.c
tests/js8_live_test.c
tests/linux_js8_live.py
tests/linux_audio.py
docs/js8/architecture.md
docs/js8/activity-log.md
docs/project/codex/T064-js8-live-qmx-rx.md
```

External behavior references:

- KFS WebSDR browser audio is the initial real source.
- PulseAudio/PipeWire sink monitor sources are ordinary recordable PCM sources.
- Modern ALSA PulseAudio plugins support selecting a Pulse source/device; prefer
  that integration rather than adding a second native desktop-audio stack.

## Architectural decisions

### 1. New Linux Audio endpoint

Add endpoint syntax:

```text
pulse:<source>
```

Examples:

```text
pulse:@DEFAULT_MONITOR@
pulse:alsa_output.pci-0000_00_1f.3.analog-stereo.monitor
```

This endpoint exists only in the Linux Audio provider. No public MiniShell API
change is needed.

The endpoint means: record from the named PulseAudio/PipeWire-Pulse source and
supply the application's requested MiniShell Audio format.

Use the existing ALSA provider infrastructure / ALSA Pulse plugin where practical
so the normal `snd_pcm_wait()`, recovery and discontinuity behavior are retained.
Do not add libpulse, PipeWire, GStreamer, ffmpeg, browser automation, HTTP or
WebSocket dependencies for this task.

The provider may open the Pulse PCM at the requested 12 kHz / S16 / stereo if the
plugin performs the desktop resampling. Do not reuse the QMX-specific assumption
that every ALSA-like source is 48 kHz / S24_3LE.

### 2. Preserve QMX behavior exactly

Existing endpoint syntax:

```text
alsa:<device>
```

must retain its accepted QMX path:

```text
48 kHz
S24_3LE
stereo
phase-0 /4 conversion
10 ms target latency
```

Do not change the proven QMX native format merely to share code with `pulse:`.

Provider state may distinguish QMX/ALSA-hardware vs Pulse-monitor mode internally.

### 3. JS8 source-delay correction

Add optional JS8 argument:

```text
--rx-delay-ms N
```

Semantics:

- integer milliseconds;
- default = 0;
- accepted range = 0..5000 ms;
- positive N means the PCM arriving now represents RF audio N milliseconds old;
- subtract N from the chunk's UTC reference **before** the existing produced-sample
  backdating / slot-anchor calculation;
- no change to the 15-second slot cadence, 1.6-second pre-roll, 89280-sample
  capture length, or candidate search;
- handle second and slot-boundary borrow correctly;
- zero must be bit/behavior equivalent to T064.

This option is generic source-latency correction, not hard-coded KFS behavior.

Do not auto-estimate or adapt delay in T066. A future improvement may use decoded
candidate timing to help calibration, but T066 keeps this operator-controlled.

### 4. Browser / WebSDR ownership

MiniShell does not:

- launch or control a browser;
- tune KFS;
- log into a WebSDR;
- open KFS HTTP/WebSocket streams;
- implement WebSDR codec/DSP;
- create a virtual sink automatically;
- change system-wide audio routing.

The operator owns browser tuning/audio start and desktop routing.

For the simplest first test, `pulse:@DEFAULT_MONITOR@` captures the default
playback sink monitor. Other desktop sounds can contaminate the receive stream, so
the operator should keep other audio quiet.

A named monitor source may be selected explicitly after inspecting:

```sh
pactl list short sources
```

A dedicated sink/routing setup is optional operator configuration and is not a
MiniShell requirement.

## CLI behavior

Accepted forms include:

```text
js8chat --rx alsa:hw:2,0
js8chat --rx /flash/test.wav
js8chat --rx pulse:@DEFAULT_MONITOR@
js8chat --rx pulse:<named-monitor> --rx-delay-ms 750
```

Update usage text to include `--rx-delay-ms N`.

`--rx-delay-ms` is valid with any RX endpoint; this keeps it generic and testable.
QMX/default behavior remains zero delay.

Duplicate, negative, nonnumeric or >5000 values are usage errors.

## KFS first-test procedure

Use the browser as normal.

For 40 m JS8 Normal, initial operator setup is approximately:

```text
frequency: 7078 kHz
mode:      USB
audio:     Start
bandwidth: enough for roughly 200..2900 Hz JS8 audio
```

KFS supports startup tuning through its normal WebSDR URL parameters, but T066
does not depend on a particular URL form.

Then on pc-1:

```sh
pactl list short sources
```

and in MiniShell:

```text
M$> js8chat --rx pulse:@DEFAULT_MONITOR@ --rx-delay-ms <measured/trial-ms> --dial-hz 7078000 --slots 20
```

Do not pass `--cat` for this remote receive path.

Because WebSDR latency is not known a priori, initial field testing may sweep a
small set such as 0/250/500/750/1000/1250/1500 ms until complete windows decode.
Do not bake any observed KFS value into the product default.

## Implementation scope

Expected files are approximately:

```text
platform/linux/linux_audio_wav.c
apps/js8chat/main/js8chat_main.c
apps/js8chat/src/live_rx/js8_live.[ch]
apps/js8chat/README.md
tests/js8_live_test.c
tests/linux_js8_live.py
Linux Audio provider tests as needed
docs/project/codex/T066-js8-websdr-audio.md
```

A small provider-private refactor is allowed if needed to separate native QMX
format handling from Pulse monitor handling. Do not redesign the public Audio API.

## Non-goals

- Direct KFS/WebSDR HTTP or WebSocket client.
- Reverse engineering the PA3FWM audio codec.
- KiwiSDR protocol support.
- WebSDR waterfall/control UI.
- Browser automation.
- Windows virtual-audio-cable support.
- ADV network audio.
- JS8 TX.
- Local QMX CAT while using WebSDR.
- Automatic source-delay estimation.
- Changes to JS8 PHY/protocol/reassembly/activity semantics.
- Changes to JS8 Normal-only product scope.
- New public MiniShell APIs.

## Acceptance criteria

- [x] Linux Audio accepts `pulse:<source>` and returns 12 kHz S16 stereo through the existing MiniShell Audio contract.
- [ ] `pulse:@DEFAULT_MONITOR@` is supported on the pc-1 desktop audio stack.
- [x] Existing `alsa:<QMX>` behavior is unchanged.
- [x] No public MiniShell API change.
- [x] No WebSDR/network/browser code enters JS8Chat.
- [x] JS8 accepts `--rx-delay-ms 0..5000`; default is zero.
- [x] Delay correction is applied before JS8 UTC slot scheduling/backdating.
- [x] Delay arithmetic is correct across UTC second and 15-second slot boundaries.
- [x] Zero-delay synthetic live output remains identical to T064.
- [x] Delayed synthetic live audio decodes with matching delay correction.
- [x] Audio discontinuity behavior remains T064-compatible.
- [x] Existing JS8/QMX and repository tests remain green.
- [x] Documentation explains browser monitor routing, KFS use, and why no `--cat` is used.
- [x] Real pc-1 browser-monitor audio can be opened and serviced continuously.
- [x] At least one real WebSDR JS8 frame is decoded when on-air activity is available; if the selected band is quiet, lack of a station is not treated as an Audio-provider failure.

## Automated tests

Add focused tests for:

1. option parsing:
   - omitted delay -> 0;
   - 0, 1, representative midrange and 5000 accepted;
   - negative, >5000, malformed and duplicate rejected;
2. timing:
   - delay subtraction with nanosecond borrow;
   - exact UTC-second boundary;
   - exact 15-second boundary;
   - produced-sample backdating still occurs after source-delay correction;
3. synthetic live integration:
   - existing zero-delay fixtures unchanged;
   - add known artificial source delay to the timed input and verify equal decode/event
     semantics when the matching `--rx-delay-ms` is supplied;
4. QMX provider regression:
   - preserve current 48 kHz / S24_3LE hardware path;
5. Pulse endpoint parsing/provider state:
   - no dependency on a real Pulse server in ordinary CI where practical.

Run at minimum:

```sh
cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"
ctest --test-dir build-linux --output-on-failure
git diff --check
```

If sanitizer coverage already has a JS8 live selection, include the changed timing
and provider code in the appropriate existing sanitizer run.

## Manual / architect validation

After supervisor review on pc-1:

1. `git pull`, rebuild.
2. Open KFS WebSDR in Firefox/Chromium, tune an active JS8 Normal band in USB,
   and click Audio Start.
3. Confirm monitor sources with `pactl list short sources`.
4. Run JS8Chat using `pulse:@DEFAULT_MONITOR@` or an explicit named monitor.
5. Adjust `--rx-delay-ms` only as needed; record the value used.
6. Run at least 20 completed slots.
7. Record final `JS8 stopped` counts, timing/drop/discontinuity diagnostics and
   any decoded activity.
8. Confirm quitting/reopening the app works and ordinary QMX `alsa:` still works.

No RF transmit is involved.

## Codex instructions

1. Read this task, T064 and the current Linux Audio provider before editing.
2. Preserve the public Audio API and QMX accepted behavior.
3. Keep the WebSDR/browser boundary in Linux platform composition; no direct
   WebSDR networking.
4. Implement one bounded reviewable commit on:
   `codex/T066-js8-websdr-audio`.
5. Update this task to REVIEW with exact files/tests/limitations and return the SHA.
6. No PR and no GitHub Actions wait.

## Codex implementation notes

### Implementation summary

Implemented against main `a4d4a2bb6f3b26ef1a64df875790c68d10dc77e5` on
`codex/T066-js8-websdr-audio`. Added a Linux provider-owned `pulse:<source>` path
using the existing dynamically loaded ALSA stack, plus generic JS8
`--rx-delay-ms 0..5000` correction (default zero). No task-scope deviations.

The ALSA Pulse configuration's `DEVICE` argument was checked against the installed
`/usr/share/alsa/alsa.conf.d/50-pulseaudio.conf` and the upstream
[ALSA plugin configuration](https://github.com/alsa-project/alsa-plugins/blob/master/pulse/50-pulseaudio.conf).
Source names are bounded to 255 ASCII letters/digits or `_`, `-`, `.`, `@`,
preventing ALSA argument injection. The provider opens `pulse:DEVICE=<source>`
nonblocking, requests S16_LE at the application's rate/channels, and returns
bounded reads with the existing wait/recovery/discontinuity contract. Mono/stereo
are supported; JS8 continues requesting 12 kHz stereo. Plugin/server errors fail
open cleanly. No new linked desktop-audio library, network client or routing change.

### Files changed

- `platform/linux/linux_audio_wav.c`: endpoint validation, Pulse native format,
  bounded S16 reads and cleanup, with unchanged QMX conversion/recovery path.
- `apps/js8chat/main/js8chat_main.c`, `src/live_rx/js8_live.[ch]`: delay option,
  lifecycle storage and correction before existing anchor/backdating.
- `src/live_rx/js8_frontend.[ch]`: pure bounded integer UTC delay subtraction.
  The existing timing helper lives here; there is no `js8_timing.c` in this baseline.
- `tests/linux_pulse_audio_test.c`, `CMakeLists.txt`: server-free provider test.
- `tests/js8_live_test.c`, `tests/js8_live_probe.c`, `tests/linux_js8_live.py`:
  parsing/borrow/reset tests and independently delayed synthetic input.
- App README, `docs/js8/activity-log.md`, this packet: setup and evidence.

### Behavior / invariants preserved

No public API, FT8 production, JS8 engine/protocol/reassembly/schema, scheduler,
CAT or host decoder changes. QMX remains blocking capture at 48 kHz S24_3LE
stereo, phase-0 /4 conversion and 10000 us target latency. Pulse uses no QMX
conversion. Its native sample format is decoded explicitly as little-endian S16.

Delay subtracts whole milliseconds from each fresh UTC reading with nanosecond
borrow and underflow checking, then the existing produced-sample backdating
runs. Zero is an identity operation. Discontinuity resets remain unchanged and
the configured delay survives reset. Slot cadence, pre-roll and capture length
are unchanged. `sizeof(Js8Live)` remains 12968 bytes on this build; no raw-audio
queue, new worker or additional persistent sample buffer was added.

### Tests run and results

```sh
cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure
# Sandbox: 116/117 passed; existing linux_serial_unit failed at line 67.
ctest --test-dir build-linux -R '^linux_serial_unit$' --output-on-failure
# Outside sandbox: 1/1 passed unchanged.
ctest --test-dir build-linux --output-on-failure
# Outside sandbox full rerun: 116/117; the same Serial assertion failed.
ctest --test-dir build-linux --repeat until-pass:5 --output-on-failure
# Final outside-sandbox run: 117/117 passed, 55.69 seconds; all passed on their first attempt.

cmake -S tests/unit -B /tmp/T064-build-unit
cmake --build /tmp/T064-build-unit -j8
ctest --test-dir /tmp/T064-build-unit --output-on-failure
# 27/27 passed, 1.20 seconds.

cmake -S . -B /tmp/T064-build-sanitize -DCMAKE_C_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer -g' -DJS8_A2_1_REFERENCE_WAV="$HOME/projects/js8chat/A_2_1.wav"
cmake --build /tmp/T064-build-sanitize -j8 --target minishell js8chat js8_live_probe js8_live_unit linux_pulse_audio_unit linux_alsa_discontinuity_unit linux_audio_discontinuity_unit js8_rx_unit js8_phy_unit js8_frame_unit js8_protocol_frame_unit js8_compound_unit js8_directed_unit js8_huffman_unit js8_jsc_unit js8_reassembly_unit js8_activity_unit js8_activity_log_unit js8_decode js8_multislot_host_unit
ctest --test-dir /tmp/T064-build-sanitize -R '^(js8_(phy_unit|rx_unit|frame_unit|protocol_frame_unit|compound_unit|directed_unit|huffman_unit|jsc_unit|reassembly_unit|activity_unit|activity_log_unit|activity_json_unit|activity_wav_unit|live_unit|wav_unit|directed_wav_unit|huffman_wav_unit|jsc_wav_unit|multislot_host_unit|multislot_wav_unit|reassembly_wav_unit|A2_1_reference)|linux_js8_live|linux_pulse_audio_unit|linux_alsa_discontinuity_unit|linux_audio_discontinuity_unit)$' --output-on-failure
# 26/26 passed, 83.23 seconds; outside sandbox for LeakSanitizer.

git diff --check
# Passed.
```

The Serial failure asserts a full PTY output queue returns TIMEOUT with zero
bytes. It repeated in isolated sandbox runs (including five retries), passed in
an isolated unsandboxed run, then failed in the first full unsandboxed rerun.
A fresh archive/build of untouched baseline `a4d4a2b` reproduced the same line-67
failure (`cmake -S /tmp/T066-baseline -B /tmp/T066-baseline-build`, build target
`linux_serial_unit`, CTest selection `^linux_serial_unit$`). The final bounded
full run above passed the Serial test on its first attempt. Serial code/tests
were not changed and no assertion was weakened.

Coverage includes omitted/zero/1/750/5000 delay, negative/malformed/overflow and
duplicate rejection, second/slot borrow, exact boundaries, negative epoch and
INT64 underflow, produced-sample backdating, and delayed discontinuity cleanup.
Existing zero-delay fixtures remain intact. Additional 0/750/5000 ms source-age
fixtures use independent fake UTC and matching CLI correction; complete live JSON
is byte-identical to the baseline for repeated HB, mixed Huffman/JSC reassembly
and four streams. The provider test verifies default/named source conversion,
invalid names, format negotiation, actual PCM channel values, QMX phase retained
across short native reads, timeout/EAGAIN, recovery, open/config failure cleanup,
close/reopen and alternate Pulse mono/rate requests, without a Pulse server.

### Local desktop observation / manual validation still required

Outside the sandbox, the installed ALSA Pulse plugin captured one second using:

```sh
arecord -D pulse:DEVICE=@DEFAULT_MONITOR@ -f S16_LE -r 12000 -c 2 -d 1 /tmp/T066-pulse-probe.wav
```

Result: `Recording WAVE ... : Signed 16 bit Little Endian, Rate 12000 Hz, Stereo`.
The sandbox attempt could not connect and was stopped; the unsandboxed probe
succeeded. No routing or playback configuration was changed.

Actual MiniShell invocation on this development host (not a pc-1 acceptance run):

```text
js8chat --rx pulse:@DEFAULT_MONITOR@ --slots 1
```

Captured output:

```text
JS8 receive monitor started (q to quit)
JS8 decoded slot=119348251 decode_us=20728 read_gap_max_us=5726 candidates=0 unique=0 drops=0 discontinuities=0
JS8 stopped slots=1 drops=0 discontinuities=0 error=none
```

The actual command used `timeout 45s env MINISHELL_ROOT=/tmp/T066-manual
MINISHELL_APP_DIR=/home/wei/projects/MiniShell/build-linux/runtime/apps
build-linux/minishell` with the invocation above on stdin. Exit status was zero;
no timeout occurred. The dictionary was copied into this temporary root.

After supervisor review, pc-1 must still perform the browser/KFS test, trial delay
selection, at least 20 slots, activity/log inspection, quit/reopen and QMX regression
specified above. No real WebSDR frame was claimed by the local silent-monitor run.

### Known limitations / risks

Requires the ALSA Pulse plugin and a running PulseAudio/PipeWire-Pulse server;
no direct libpulse fallback. Ordinary CI uses injected provider functions.
Source latency is a fixed operator-selected correction; jitter/drift are not
estimated. Other desktop sounds can contaminate monitor capture. No browser
control, automatic sink creation, network access or local CAT for WebSDR is added.
Manual setup and source-name constraints are documented in the app README.

### Commit reference

One bounded commit on `codex/T066-js8-websdr-audio`; SHA returned in the handoff.
No PR or GitHub Actions wait.

## Supervisor review

Reviewed commit `1f63b0732cdfbcd628cef3875ed4d6d24daac7d8` against T066 and the accepted T064 live-QMX baseline.

Result: **PASS — implementation accepted for pc-1 WebSDR/browser testing.**

Review findings:

- One bounded implementation commit, exactly one commit ahead of baseline `a4d4a2bb6f3b26ef1a64df875790c68d10dc77e5`.
- No public MiniShell API, JS8 PHY/protocol/reassembly/activity schema, CAT, scheduler geometry or FT8 production changes.
- Existing `alsa:` QMX behavior remains isolated at 48 kHz / S24_3LE / stereo, phase-0 /4 conversion and 10 ms target latency.
- New `pulse:` handling stays entirely inside the Linux Audio provider and uses the existing dynamically loaded ALSA stack; no libpulse/PipeWire/network/browser dependency was introduced.
- `pulse:<source>` maps to ALSA `pulse:DEVICE=<source>`. The accepted source-name character filter is bounded and prevents ALSA-argument injection while covering normal Pulse/PipeWire monitor names and `@DEFAULT_MONITOR@`.
- The upstream ALSA Pulse plugin configuration accepts a named `DEVICE` argument exactly in this form.
- Pulse capture negotiates requested S16_LE rate/channels directly and bypasses the QMX decimator. Provider tests cover stereo/mono PCM, named/default sources, short reads, EAGAIN, recovery, failure cleanup, reopen and coexistence exclusion.
- `--rx-delay-ms` is bounded to 0..5000, defaults to zero and is retained across discontinuity reset.
- Delay subtraction occurs on each fresh UTC reading before the existing produced-sample backdating and slot anchor. Nanosecond borrow, second/slot boundaries, negative epoch and INT64 underflow are covered.
- Zero-delay integration remains byte-identical to the T064 baseline. Artificial 750 ms and 5000 ms source-age cases decode to identical event bytes and slot identities when matching correction is supplied.
- Real local desktop evidence is sufficient for supervisor review: ALSA Pulse captured 12 kHz S16 stereo from `@DEFAULT_MONITOR@`, and an actual MiniShell JS8 one-slot run completed with drops=0, discontinuities=0 and error=none.
- The intermittent `linux_serial_unit` failure is not caused by T066: Serial sources/tests are unchanged, the same line-67 failure reproduces from an untouched baseline build, and the final bounded full suite passed 117/117 without retrying individual tests.
- Final reported gates are consistent with the diff: Linux 117/117, portable 27/27, sanitizer 26/26, plus `git diff --check`.

No blocking defect or architecture deviation found.

Main was fast-forwarded to the reviewed implementation commit. T066 now enters TESTING for the real pc-1 browser/KFS path. The remaining gate is operator/browser routing, delay selection and live WebSDR reception; a quiet band does not invalidate the Pulse provider.

## Architect test result

### pc-1 KFS WebSDR acceptance — PASS

The architect used the active PipeWire monitor source:

```text
alsa_output.pci-0000_00_1f.3.analog-stereo.monitor
```

Browser audio changed that source from SUSPENDED to RUNNING. MiniShell was then run with:

```text
M$> js8chat --rx pulse:alsa_output.pci-0000_00_1f.3.analog-stereo.monitor --rx-delay-ms 0 --dial-hz 7078000 --slots 20
```

Real KFS/WebSDR JS8 traffic decoded immediately. Accepted evidence included:

```text
JS8 decoded slot=119348340 ... candidates=50 unique=0 drops=0 discontinuities=0
JS8 decoded slot=119348341 ... candidates=50 unique=0 drops=0 discontinuities=0
JS8 decoded slot=119348342 ... candidates=50 unique=0 drops=0 discontinuities=0
2026-09-24T04:45:45Z audio_millihz=706250 HB KC0CYR AP90
JS8 decoded slot=119348343 ... candidates=50 unique=1 drops=0 discontinuities=0
2026-09-24T04:46:00Z audio_millihz=753125 DIRECTED W8RAY -> K1CF
2026-09-24T04:46:00Z audio_millihz=959375 DIRECTED KK7UMM -> KC0CYR
2026-09-24T04:46:00Z audio_millihz=559375 DIRECTED KL7UT -> KC0CYR
2026-09-24T04:46:00Z audio_millihz=509375 DIRECTED K8IMT -> KC0CYR
2026-09-24T04:46:00Z audio_millihz=906250 DIRECTED WD5EED -> KC0CYR
2026-09-24T04:46:00Z audio_millihz=606250 DIRECTED WB7TSQ -> KC0CYR
JS8 decoded slot=119348344 ... candidates=50 unique=6 drops=0 discontinuities=0
```

Observed read-gap maximum was about 6.1 ms and decode time about 46-70 ms, with zero drops and zero Audio discontinuities in the accepted sample. The architect determined that the KFS/browser/network latency is already within the existing JS8 timing/search tolerance on this path, so `--rx-delay-ms 0` is accepted for current KFS use; no hard-coded delay is introduced.

The explicit named monitor source is the accepted pc-1 path. `pulse:@DEFAULT_MONITOR@` remains supported by implementation/provider tests but was not required for final acceptance because the active named monitor was unambiguous.

T066 is COMPLETE. The accepted Linux JS8 RX inputs now include local QMX ALSA and browser/WebSDR Pulse/PipeWire monitor audio.

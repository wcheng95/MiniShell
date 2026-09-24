# T066 — JS8 Linux WebSDR/browser audio input

Status: READY

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

- [ ] Linux Audio accepts `pulse:<source>` and returns 12 kHz S16 stereo through the existing MiniShell Audio contract.
- [ ] `pulse:@DEFAULT_MONITOR@` is supported on the pc-1 desktop audio stack.
- [ ] Existing `alsa:<QMX>` behavior is unchanged.
- [ ] No public MiniShell API change.
- [ ] No WebSDR/network/browser code enters JS8Chat.
- [ ] JS8 accepts `--rx-delay-ms 0..5000`; default is zero.
- [ ] Delay correction is applied before JS8 UTC slot scheduling/backdating.
- [ ] Delay arithmetic is correct across UTC second and 15-second slot boundaries.
- [ ] Zero-delay synthetic live output remains identical to T064.
- [ ] Delayed synthetic live audio decodes with matching delay correction.
- [ ] Audio discontinuity behavior remains T064-compatible.
- [ ] Existing JS8/QMX and repository tests remain green.
- [ ] Documentation explains browser monitor routing, KFS use, and why no `--cat` is used.
- [ ] Real pc-1 browser-monitor audio can be opened and serviced continuously.
- [ ] At least one real WebSDR JS8 frame is decoded when on-air activity is available; if the selected band is quiet, lack of a station is not treated as an Audio-provider failure.

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

Codex fills this section before handoff.

## Supervisor review

Supervisor fills this after reviewing the actual diff and test evidence.

## Architect test result

Record pc-1 browser/WebSDR validation and final acceptance here.

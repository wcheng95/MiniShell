# MiniFT8-V3

MiniFT8-V3 is a substantial portable application hosted by MiniShell. This directory is the canonical documentation for active MiniFT8-V3 development.

The previous standalone MiniFT8-V3 repository has been superseded. Active development and authoritative architecture now live with MiniShell so application requirements and MiniShell API evolution can be developed and tested together.

## Current boundary

```text
MiniFT8-V3 project
    |
ft8 runtime application
    |
MiniShell API
    |
MiniShell services/runtime
    |
Linux / future NuttX / thick embedded backend / mocks
```

MiniFT8 contains no Linux, ncurses, ESP-IDF, NuttX, USB/UART/I2S, or board-specific path in its application core.

Code-facing names use lowercase; prose continues to use the project names MiniShell and MiniFT8.

## Protocol application boundary

MiniFT8 is now the project/design name while the MiniShell runtime application is simply:

```text
ft8
```

The runtime application is **FT8-only**. Protocol switching is handled by MiniShell application switching rather than an internal mode selector:

```text
ft8      current
ft4      future
cw       future
rtty     future
js8      future
```

Future protocol applications are created only when their implementation begins. No placeholder apps are maintained merely to reserve names.

## Major MiniFT8 domain blocks

With platform/storage responsibilities moved below MiniShell, the remaining major FT8 radio-domain blocks are intentionally small in number:

```text
RX
AutoSeq
TX
ADIF log
```

They are coordinated through `app_controller`; they do not call one another behind it.

## Current priority

RX-0 architecture/source review, RX-1A golden-boundary freeze, RX-1B top-down ownership design, and the P1/P2/V1 platform/presentation checkpoint are complete.

Validated matrix:

```text
Linux backend + DESKTOP presentation   PASS
Linux backend + ADV presentation       PASS
ADV backend   + ADV presentation       PASS
```

Real ADV P2/V1 testing also established the current pre-RX memory baseline:

```text
heap free       ~282 KiB
largest block   ~228 KiB
```

The active stage is:

```text
RX-1C  clean monitor ownership/workspace/lifecycle
```

RX-1C begins source migration only after RX-1B fixed the ownership contracts. It preserves the V2 6 kHz monitor mathematics and must reproduce the RX-1A waterfall exactly.

Canonical current plans:

```text
rx.md
rx-1b-design.md
development.md
```

## Current integrated baseline

MiniShell-native MiniFT8 currently contains:

```text
text UI
DESKTOP and ADV presentation profiles
configuration
prototype scheduler settings
station.txt persistence
MiniShell Display/Input/Filesystem integration
MiniShell Audio API + deterministic WAV RX provider
```

Application I/O is modeled as three independent resources:

```text
RX Audio Path
TX Audio Path
Control Path
```

MiniFT8 requests Audio V1 transport as:

```text
12000 Hz
signed 16-bit PCM
2 channels
```

MiniShell preserves channel ordering but does not assign channel meaning. Ordinary audio versus I/Q is a MiniFT8 source/profile contract.

During the RX ownership-cleanup stages, `rx_frontend` adapts the MiniShell transport to the locked FT8-engine boundary:

```text
6000 Hz
mono float
960 samples per FT8 monitor block
```

This preserves proven MiniFT8-V2 decoder behavior. Changing the engine sample rate, FFT/OSR, filters, candidate policy, LDPC behavior, or SNR estimator is explicitly deferred to later algorithm work.

Examples of future source semantics:

```text
RX = QMX-AUDIO
    channel 0/1 = ordinary audio channels
    MiniFT8 selects/downmixes as needed

RX = QMX-IQ
    channel 0 = I
    channel 1 = Q
    MiniFT8 uses the I/Q DSP path
```

Hardware-native transport conversion stays below MiniShell; protocol-specific DSP, channel interpretation, and TX waveform synthesis stay inside MiniFT8.

Control remains a planned independent MiniShell service. Its eventual generic operations must not expose FT8 symbols or device-specific CAT syntax.

## RX ownership shape

RX-1B fixed five application-level modules beneath `app_controller`:

```text
app_controller
    |
    +-- rx_audio_adapter
    +-- rx_frontend
    +-- rx_slot_framer
    +-- ft8_engine
    `-- rx_result_builder
```

`app_controller` remains the only coordinator. `ft8_engine` owns the logical use of monitor/waterfall/candidate/decode/hash state but has no MiniShell, UI, storage, AutoSeq, TX, or platform dependency.

## RX memory rule

Normal RX is streaming:

```text
bounded PCM buffers
    -> bounded FFT workspace
    -> whole decode-window waterfall
    -> decode
```

A whole raw-audio slot is not required. Optional research modes may retain or double-buffer raw PCM so alternate algorithms can consume the same samples, but Cardputer-class operation must remain possible without that memory cost.

Locked rule:

> Stream raw audio; retain the waterfall; retain raw PCM only by explicit exception.

The V2-compatible 6 kHz/time_osr=2/freq_osr=1 monitor uses roughly 80.5 KiB for the magnitude waterfall plus FFT/history/scratch workspace. RX-1C will make the exact cleaned workspace requirement queryable rather than hiding it in mutable globals.

## Run

Build MiniShell normally, then:

```text
M$> ft8                    # DESKTOP default
M$> ft8 --profile desktop
M$> ft8 --profile adv
```

`q` exits MiniFT8 and returns to:

```text
M$>
```

Presentation selection is launch policy. It is not persisted and is not inferred from `platform=linux` or `platform=adv`.

The current configuration file is:

```text
/flash/ft8/station.txt
```

Configuration saves use:

```text
write /flash/ft8/station.txt.tmp
sync + close
rename -> /flash/ft8/station.txt
```

through the MiniShell Filesystem API.

The current FT8-only configuration uses scalar protocol-local values such as:

```text
profile=
band=
skip_tx1=
max_retry=
```

There is no persisted protocol `mode=` field and no `presentation=` field inside `station.txt`.

The O-screen `Profile: Default` setting is a station/operating profile and is distinct from the ADV/DESKTOP presentation.

## Documentation

- `architecture.md` — ownership, dependency direction, Audio and RX/TX/Control boundaries.
- `rx.md` — canonical decode-RX pipeline, RAM rules, V2 classification, golden-reference policy, and staged RX development plan.
- `rx-1b-design.md` — locked RX-1B physical module boundaries, ownership, 6 kHz engine contract, lifecycle, workspace, error, and unit-test responsibilities.
- `rx-decoder-contract.md` — RX-0B review of V2 `decode_helper.cpp` and the extracted decoder contract.
- `rx-v2-production-review.md` — RX-0B review of production `decode_monitor_results()`, with mixed V2 responsibilities assigned to V3 owners.
- `rx-monitor-review.md` — RX-0B review of `monitor.h/c`, DSP/workspace ownership, reset semantics, RAM requirements, and monitor-level golden strategy.
- `rx-decode-review.md` — RX-0B review of `decode.h/c`, candidate search, likelihood/LDPC/CRC boundaries, status cleanup, and deep-search extension points.
- `rx-message-review.md` — RX-0B review of `message.h/c`, typed protocol results, callsign-hash ownership, special-message handling, and codec gaps.
- `rx-golden.md` — RX-1A pinned V2 golden boundaries: reference WAVs, exact Linux waterfall fingerprints, payload/codec vectors, and known V2 gaps that are not golden targets.
- `v1-validation.md` — P1/P2/V1 cross-backend/profile validation record.
- `ui.md` — presentation geometry, UI model, and controls.
- `development.md` — current development gate and next task.

## Source

Current implemented application source:

```text
apps/ft8/
├── main/                  MiniShell application edge/adapters
├── include/ft8/           shared application types
└── src/
    ├── app_controller/
    ├── config_service/
    ├── presentation_profile/
    ├── qso_scheduler/
    ├── storage_service/
    └── ui_shell/
```

RX-1B reserves the next ownership modules as implementation begins:

```text
    ├── rx_audio_adapter/
    ├── rx_frontend/
    ├── rx_slot_framer/
    ├── ft8_engine/
    └── rx_result_builder/
```

Do not create placeholder modules merely to mirror the design document. Add each source module when its implementation/test stage begins.

As functionality grows, new modules should be introduced only when their ownership and interfaces are clear. A logical owner may use several small private implementation files; one-owner does not mean one giant source file.

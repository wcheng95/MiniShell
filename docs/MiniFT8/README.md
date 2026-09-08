# MiniFT8-V3

MiniFT8-V3 is a substantial portable application hosted by MiniShell. This directory is the canonical documentation for active MiniFT8-V3 development.

The previous standalone MiniFT8-V3 repository has been superseded. Active development and authoritative architecture now live with MiniShell so application requirements and MiniShell API evolution can be developed and tested together.

## Current boundary

```text
MiniFT8-V3
    |
MiniShell API
    |
MiniShell services/runtime
    |
Linux / future NuttX / thick embedded backend / mocks
```

MiniFT8 contains no Linux, ncurses, ESP-IDF, NuttX, USB/UART/I2S, or board-specific path in its application core.

## Major MiniFT8 domain blocks

With platform/storage responsibilities moved below MiniShell, the remaining major radio-domain blocks are intentionally small in number:

```text
RX
AutoSeq
TX
ADIF log
```

They are coordinated through `app_controller`; they do not call one another behind it.

## Current priority

RX-0 architecture/source review and RX-1A golden-boundary freeze are complete. RX-1B is **paused, not abandoned** while the architecture is exercised across two MiniShell backends and two MiniFT8 profiles.

Current validation matrix:

```text
Linux backend + DESKTOP profile
Linux backend + ADV profile
ADV backend   + ADV profile
```

The key comparison is Linux + ADV profile versus real ADV + ADV profile: same MiniFT8 core/profile, different MiniShell backend.

Canonical current plan:

```text
../project/adv-backend-plan.md
```

After its V1 validation checkpoint passes, development resumes at RX-1B: top-down RX module/interface design.

## Current integrated baseline

MiniShell-native MiniFT8 currently contains:

```text
text UI
configuration
prototype scheduler settings
Station.txt persistence
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

Examples:

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

## Run

Build MiniShell normally, then:

```text
M$> MiniFT8
```

`q` exits MiniFT8 and returns to:

```text
M$>
```

The current configuration file is:

```text
/flash/MiniFT8/Station.txt
```

Configuration saves use:

```text
write /flash/MiniFT8/Station.txt.tmp
sync + close
rename -> /flash/MiniFT8/Station.txt
```

through the MiniShell Filesystem API.

## Documentation

- `architecture.md` — ownership, dependency direction, Audio and RX/TX/Control boundaries.
- `rx.md` — canonical decode-RX pipeline, RAM rules, V2 classification, golden-reference policy, and staged RX development plan.
- `rx-decoder-contract.md` — RX-0B review of V2 `decode_helper.cpp` and the extracted decoder contract.
- `rx-v2-production-review.md` — RX-0B review of production `decode_monitor_results()`, with mixed V2 responsibilities assigned to V3 owners.
- `rx-monitor-review.md` — RX-0B review of `monitor.h/c`, DSP/workspace ownership, reset semantics, RAM requirements, and monitor-level golden strategy.
- `rx-decode-review.md` — RX-0B review of `decode.h/c`, candidate search, likelihood/LDPC/CRC boundaries, status cleanup, and deep-search extension points.
- `rx-message-review.md` — RX-0B review of `message.h/c`, typed protocol results, callsign-hash ownership, special-message handling, and codec gaps.
- `rx-golden.md` — RX-1A pinned V2 golden boundaries: reference WAVs, exact Linux waterfall fingerprints, payload/codec vectors, and known V2 gaps that are not golden targets.
- `ui.md` — current UI model and controls.
- `development.md` — current development gate and next task.

## Source

```text
apps/MiniFT8/
├── main/                  MiniShell application edge/adapters
├── include/minift8/       shared application types
└── src/
    ├── app_controller/
    ├── config_service/
    ├── qso_scheduler/
    ├── storage_service/
    └── ui_shell/
```

As functionality grows, new modules should be introduced only when their ownership and interfaces are clear. A logical owner may use several small private implementation files; one-owner does not mean one giant source file.

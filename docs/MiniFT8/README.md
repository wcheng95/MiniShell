# MiniFT8-V3

MiniFT8-V3 is a substantial portable application hosted by MiniShell. This directory is the canonical documentation for active MiniFT8-V3 development.

The previous standalone MiniFT8-V3 repository has been superseded. Active development and authoritative architecture now live with MiniShell so application requirements and MiniShell ABI evolution can be developed and tested together.

## Current boundary

```text
MiniFT8-V3
    |
MiniShell ABI
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

The next milestone is **decode RX**. Its canonical architecture and staged development plan are in `rx.md`.

## Current integrated baseline

MiniShell-native MiniFT8 currently contains:

```text
30x8 text UI
configuration
prototype scheduler settings
Station.txt persistence
MiniShell Display/Input/Filesystem integration
MiniShell Audio ABI + deterministic WAV RX provider
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
    -> whole-slot waterfall
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

through the MiniShell Filesystem ABI.

## Documentation

- `architecture.md` — ownership, dependency direction, Audio and RX/TX/Control boundaries.
- `rx.md` — canonical decode-RX pipeline, RAM rules, V2 classification, golden-reference policy, and RX-0 through RX-7 plan.
- `rx-decoder-contract.md` — RX-0B source review of V2 `decode_helper.cpp` and the extracted V3 decoder contract.
- `ui.md` — current 30x8 UI model and controls.
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

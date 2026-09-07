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

## Current integrated milestone

The first MiniShell-native slice intentionally contains:

```text
30x8 text UI
configuration
prototype scheduler settings
Station.txt persistence
MiniShell Display/Input/Filesystem integration
```

The next application-driven boundary is now architecturally defined:

```text
RX Audio Path
TX Audio Path
Control Path
```

These are independent logical resources rather than one monolithic radio selection.

MiniFT8 V1 requests normalized audio as:

```text
12000 Hz
signed 16-bit PCM
mono
```

The MiniShell Audio ABI remains format-capable, but initial implementations may support only that format. Hardware-native conversion stays below MiniShell; protocol-specific DSP and TX waveform synthesis stay inside MiniFT8.

The planned Control ABI exposes generic radio operations and capabilities such as dial frequency, radio mode, TX begin/end, and optional dynamic TX RF-frequency control. It does not expose FT8 symbols or device-specific CAT syntax.

Typical resource compositions are:

```text
QMX
    RX      = QMX UAC
    TX      = None
    CONTROL = QMX CAT

QDX
    RX      = QDX UAC
    TX      = QDX UAC
    CONTROL = QDX CAT
```

Audio and Control are design baselines here; they are not yet claims that those MiniShell services are implemented.

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

- `architecture.md` — ownership, dependency direction, and the RX/TX/Control boundary.
- `ui.md` — current 30x8 UI model and controls.
- `development.md` — development rules, tests, Audio/Control baseline, and next vertical slice.

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

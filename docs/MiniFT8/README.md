# MiniFT8-V3

MiniFT8-V3 is a substantial portable application hosted by MiniShell. This directory is the canonical documentation for active MiniFT8-V3 development.

The earlier standalone `wcheng95/MiniFT8-V3` repository remains the pre-MiniShell historical reference. Active development now lives with MiniShell so application requirements and MiniShell ABI evolution can be developed and tested together.

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

MiniFT8 contains no Linux, ncurses, ESP-IDF, NuttX, or board-specific path in its application core.

## Current integrated milestone

The first MiniShell-native slice intentionally contains only:

```text
30x8 text UI
configuration
prototype scheduler settings
Station.txt persistence
MiniShell Display/Input/Filesystem integration
```

Not yet part of this milestone:

```text
Audio
Radio/CAT
QMX
ft8_engine / decode
TX
logging
GPS integration
```

This small starting point proves the application lifecycle and platform boundary before adding radio/DSP complexity.

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

- `architecture.md` — ownership, dependency direction, and module boundaries.
- `ui.md` — current 30x8 UI model and controls.
- `development.md` — development rules, tests, and next vertical slice.

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

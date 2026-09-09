# MiniFT8

MiniFT8-V3 is a substantial portable MiniShell application. Its MiniShell runtime name is `ft8`.

Application source lives here; the canonical application documentation lives under:

```text
docs/MiniFT8/
```

The application core depends on MiniShell services rather than Linux, NuttX, ESP-IDF, board APIs, or test mocks. Platform-specific providers stay below the MiniShell API.

The runtime application is intentionally **FT8-only**. Other protocols such as FT4, CW, RTTY, and JS8 are separate future MiniShell applications rather than modes inside `ft8`.

## Presentation profiles

P1 defines two MiniFT8 application presentations:

```text
DESKTOP   30 x 8 text frame, including a contextual footer
ADV       20 x 7 text frame, six main lines and no footer
```

They use the same controller, configuration, scheduler settings, `UiModel`, navigation, and MiniShell Display/Input adapter.

Presentation is launch/composition policy rather than backend identity or station configuration.

Linux:

```text
M$> ft8                    # DESKTOP default
M$> ft8 --profile desktop
M$> ft8 --profile adv
```

Current development policy is Linux backend first and ADV presentation for MiniFT8 UI work until a real ADV backend dependency must be exercised.

Cardputer ADV statically packages the same MiniFT8 source files and supplies `ADV` as the composition default:

```text
M$> ft8                    # ADV default on the ADV firmware
```

There is no runtime platform check inside MiniFT8 to select this. The small ADV static wrapper only supplies the application default during composition.

The ADV/DESKTOP presentation is not persisted in `/flash/ft8/station.txt`. The O-screen `Profile: Default` item is a separate station/operating-profile concept.

## P1/P2/V1 status

The cross-backend/profile checkpoint is complete.

```text
Linux + DESKTOP   PASS
Linux + ADV       PASS
ADV   + ADV       PASS
```

Validated behavior includes:

```text
app discovery and foreground lifecycle
shared UI actions/state transitions
ADV text 20x7 presentation
configuration persistence through /flash/ft8/station.txt
repeated ADV ft8 launch/exit cycles
stable ADV memory baseline
no direct platform dependencies under apps/ft8/
```

The canonical checkpoint record is:

```text
docs/MiniFT8/v1-validation.md
```

During P2, MiniShell also moved ADV foreground applications onto a dedicated 16 KiB application task instead of borrowing ESP-IDF's `app_main` stack. The portable `free` utility provides a useful ADV baseline before RX/DSP work:

```text
heap free       about 282 KiB
largest block   about 228 KiB
app allocations 0 after ft8 exits
```

The baseline remained effectively unchanged across repeated `ft8` launch/exit cycles.

## RX status

RX-1 is complete. The cleaned `Ft8Engine` now owns the platform-independent 6 kHz FT8 protocol/DSP core:

```text
6 kHz mono float
    -> Ft8Engine
       -> monitor/waterfall
       -> candidate search
       -> likelihood/LDPC/CRC
       -> Ft8HashStore
       -> typed protocol codec
       -> exact-payload dedupe
    -> Ft8ProtocolSlot
```

RX-2 is implemented as a Linux host development utility:

```text
apps/ft8/tools/ft8_decode.c
```

It is not a MiniShell runtime application and has no presentation profile. It accepts one 6 kHz/mono/S16 PCM WAV decode window and prints every unique decoded FT8 message to stdout.

Example:

```text
./build/ft8_decode ft8_cq_w1xyz_fn42.wav
CQ W1XYZ FN42
```

The pinned RX-2 CI reference and full Linux CI pass. RX-2 remains the active stage until the same utility is validated by the user on `pc-1`.

Canonical RX-2 record:

```text
docs/MiniFT8/rx-2-host-decoder.md
```

After pc-1 validation, RX-3 adds the MiniFT8-owned frontend:

```text
12 kHz S16 two-channel
    -> rx_frontend
    -> 6 kHz mono float
    -> same Ft8Engine
```

Current application integration still includes the text UI, configuration, scheduler settings, and persistent `/flash/ft8/station.txt` through MiniShell Display, Input, and Filesystem services. MiniShell Audio V1 and the deterministic Linux WAV RX provider are also implemented, but they are not yet connected to the cleaned RX pipeline.

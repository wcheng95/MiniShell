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

Cardputer ADV P2 statically packages the same MiniFT8 source files and supplies `ADV` as the composition default:

```text
M$> ft8                    # ADV default on the ADV firmware
```

There is no runtime platform check inside MiniFT8 to select this. The small ADV static wrapper only supplies the application default during composition.

The ADV/DESKTOP presentation is not persisted in `/flash/ft8/station.txt`. The O-screen `Profile: Default` item is a separate station/operating-profile concept.

## P2 status

P2 is complete on real Cardputer ADV hardware.

Validated behavior:

```text
apps lists ft8
ft8 launches with Presentation: ADV
UI reports text 20x7
configuration changes persist across ft8 launches
q returns cleanly to M$>
repeated ft8 cycles remain stable
```

During P2, MiniShell also moved ADV foreground applications onto a dedicated 16 KiB application task instead of borrowing ESP-IDF's `app_main` stack. The portable `free` utility provides a useful ADV baseline before RX/DSP work:

```text
heap free       about 282 KiB
largest block   about 228 KiB
app allocations 0 after ft8 exits
```

The baseline remained effectively unchanged across repeated `ft8` launch/exit cycles.

The next gate is V1: compare Linux+ADV and ADV+ADV application-visible behavior, then resume RX-1B.

Current application integration includes the text UI, configuration, scheduler settings, and persistent `/flash/ft8/station.txt` through MiniShell Display, Input, and Filesystem services.

MiniShell Audio V1 and the deterministic Linux WAV RX provider are also implemented. RX Audio, TX Audio, and Control remain independent application resources. Control/Radio, FT8 decoding, TX realization, and logging are not yet integrated into the application.

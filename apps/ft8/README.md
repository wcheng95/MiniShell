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

Presentation is launch policy rather than backend identity or station configuration:

```text
M$> ft8                    # DESKTOP default
M$> ft8 --profile desktop
M$> ft8 --profile adv
```

The ADV/DESKTOP presentation is not persisted in `/flash/ft8/station.txt`. The O-screen `Profile: Default` item is a separate station/operating-profile concept.

Linux integration tests exercise both DESKTOP and ADV presentations. P2 will package `ft8` into the Cardputer ADV static registry and launch the ADV presentation there.

Current application integration includes the text UI, configuration, scheduler settings, and persistent `/flash/ft8/station.txt` through MiniShell Display, Input, and Filesystem services.

MiniShell Audio V1 and the deterministic Linux WAV RX provider are also implemented. RX Audio, TX Audio, and Control remain independent application resources. Control/Radio, FT8 decoding, TX realization, and logging are not yet integrated into the application.

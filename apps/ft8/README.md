# MiniFT8

MiniFT8-V3 is a substantial portable MiniShell application. Its MiniShell runtime name is `ft8`.

Application source lives here; the canonical application documentation lives under:

```text
docs/MiniFT8/
```

The application core depends on MiniShell services rather than Linux, NuttX, ESP-IDF, board APIs, or test mocks. Platform-specific providers stay below the MiniShell API.

The runtime application is intentionally **FT8-only**. Other protocols such as FT4, CW, RTTY, and JS8 are separate future MiniShell applications rather than modes inside `ft8`.

Current application integration includes the text UI, configuration, scheduler settings, and persistent `/flash/ft8/station.txt` through MiniShell Display, Input, and Filesystem services.

MiniShell Audio V1 and the deterministic Linux WAV RX provider are also implemented. RX Audio, TX Audio, and Control remain independent application resources. Control/Radio, FT8 decoding, TX realization, and logging are not yet integrated into the application.

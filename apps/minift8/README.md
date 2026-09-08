# MiniFT8

MiniFT8-V3 is a substantial portable MiniShell application.

Code-facing names use lowercase. The application source lives under:

```text
apps/minift8/
```

Canonical application documentation remains under:

```text
docs/MiniFT8/
```

The application core depends on MiniShell services rather than Linux, NuttX, ESP-IDF, board APIs, or test mocks. Platform-specific providers stay below the MiniShell API.

Current application integration includes the 30x8 UI, configuration, scheduler settings, and persistent `/flash/minift8/station.txt` through MiniShell Display, Input, and Filesystem services.

MiniShell Audio V1 and the deterministic Linux WAV RX provider are also implemented. RX-1B is currently paused while the ADV backend/profile checkpoint is completed.

RX Audio, TX Audio, and Control remain independent application resources. Control/Radio, FT8 decoding, TX realization, and logging are not yet integrated into the MiniFT8 application.

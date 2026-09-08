# MiniFT8

MiniFT8-V3 is a substantial portable MiniShell application.

Application source lives here; the canonical application documentation lives under:

```text
docs/MiniFT8/
```

The application core depends on MiniShell services rather than Linux, NuttX, ESP-IDF, board APIs, or test mocks. Platform-specific providers stay below the MiniShell ABI.

Current application integration includes the 30x8 UI, configuration, scheduler settings, and persistent `Station.txt` through MiniShell Display, Input, and Filesystem services.

MiniShell Audio V1 and the deterministic Linux WAV RX provider are also implemented. The next MiniFT8 integration slice is:

```text
tests/kfs16b12k.wav
    -> MiniShell Audio ABI
    -> MiniFT8 RX source/profile interpretation
    -> ordinary-audio downmix
    -> ft8_engine
    -> decoded RX UI
```

RX Audio, TX Audio, and Control remain independent application resources. Control/Radio, FT8 decoding, TX realization, and logging are not yet integrated into the MiniFT8 application.

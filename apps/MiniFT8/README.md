# MiniFT8

MiniFT8-V3 is a substantial portable MiniShell application.

Application source lives here; the canonical application documentation lives under:

```text
docs/MiniFT8/
```

The application core must depend on MiniShell services rather than Linux, NuttX, ESP-IDF, board APIs, or test mocks. Platform-specific providers stay below the MiniShell ABI.

Current integrated milestone: 30x8 UI, configuration, and persistent `Station.txt` through MiniShell Display, Input, and Filesystem services. Audio, Radio/CAT, FT8 decoding, TX, and logging are intentionally not part of this first slice.

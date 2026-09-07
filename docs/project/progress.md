# MiniShell Progress Log

Major current state:

- Linux Mint on `pc-1` is the reference/full production target.
- Runtime `.so` application discovery/loading and the `M$>` shell are established.
- System, Memory, Filesystem, Time/Location, Display, Input, and Audio services have unit/integration coverage.
- Portable utilities and MiniFT8 run as MiniShell applications.
- Historical Tab5/ESP-IDF proof-of-concept work is preserved on `archive/tab5-legacy`, not active `main`.
- MiniFT8's UI/config/storage slice is integrated.
- Audio V1 is implemented with independent RX/TX APIs. MiniFT8 requests 12 kHz/S16/two-channel transport; MiniShell does not interpret stereo versus I/Q.
- The Linux WAV provider streams `tests/kfs16b12k.wav` unchanged and CI validates exact replay/cleanup.
- Control remains independent of Audio; device `set_time` is a deferred future Control capability.

## Current housekeeping pass

Before further MiniFT8 DSP/radio expansion:

```text
H1 split oversized Linux backend
H2 remove POSIX loader details from portable core
H3 split Filesystem private helpers while preserving one owner
```

After H1-H3, evaluate the cost of H5 (stateful ANSI/CSI parser) before deciding whether to implement it immediately.

## Next development boundary after housekeeping

```text
tests/kfs16b12k.wav
    -> MiniShell WAV Audio provider
    -> 12 kHz / S16 / 2-channel
    -> MiniFT8 source/profile interpretation
    -> ft8_engine
    -> decoded RX UI
```

Live QMX/UAC follows deterministic replay and backend cleanup.

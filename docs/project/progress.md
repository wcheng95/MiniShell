# MiniShell Progress Log

Current baseline:

- Linux Mint on `pc-1` is the reference/full production target.
- Runtime app discovery/loading and `M$>` are established.
- System, Memory, Filesystem, Time/Location, Display, Input, and Audio services have automated coverage.
- MiniFT8 is integrated for UI/config/storage.
- Audio V1 is implemented; MiniFT8 requests 12 kHz/S16/two-channel transport with channel meaning owned by MiniFT8.
- Linux deterministic WAV RX replays `tests/kfs16b12k.wav` unchanged.
- Control remains independent from Audio; device `set_time` is deferred.
- Historical Tab5 work is preserved on `archive/tab5-legacy`.

Current housekeeping before DSP/radio expansion:

```text
H1 split Linux backend
H2 remove POSIX loader semantics from portable core
H3 split Filesystem private helpers
```

After H1-H3, evaluate H5 stateful ANSI/CSI parser cost.

Next MiniFT8 slice afterward is deterministic WAV -> MiniFT8 channel interpretation -> `ft8_engine` -> decoded RX UI, followed later by live QMX/UAC.

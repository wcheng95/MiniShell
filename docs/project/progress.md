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

## Housekeeping paydown completed

The architecture-audit debt H1-H5 is now paid:

```text
H1 Linux backend split
H2 POSIX loader semantics removed from portable core
H3 Filesystem private helpers split while preserving one owner
H4 legacy Tab5 active tree removed and archived
H5 terminal ANSI/CSI + UTF-8 parser made stateful across reads
```

H5 is private to the Linux terminal backend. It adds a small byte-stream parser, a 30 ms standalone-Escape ambiguity window, deterministic parser split-boundary tests, and PTY integration coverage for split CSI, split UTF-8, and standalone Escape.

The full Linux integration suite and strict unit suite remain green.

## Next MiniFT8 boundary

Housekeeping no longer blocks application work. The next planned slice is:

```text
tests/kfs16b12k.wav
    -> MiniShell WAV Audio provider
    -> 12 kHz / S16 / 2-channel
    -> MiniFT8 source/profile interpretation
    -> ft8_engine
    -> decoded RX UI
```

Live QMX/UAC follows deterministic replay.

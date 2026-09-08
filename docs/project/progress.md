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

PR #13 resolved the three active internal architecture debts:

```text
H1 Linux backend split
H2 POSIX loader semantics removed from portable core
H3 Filesystem private helpers split while preserving one owner
```

The full Linux integration suite and strict unit suite remained green after the refactor.

The only remaining audit item is H5: stateful handling of terminal ANSI/CSI sequences split across transport reads. Its implementation cost is being evaluated separately before deciding whether it should block MiniFT8 DSP work.

## Next MiniFT8 boundary

After the H5 decision, the next planned application slice is:

```text
tests/kfs16b12k.wav
    -> MiniShell WAV Audio provider
    -> 12 kHz / S16 / 2-channel
    -> MiniFT8 source/profile interpretation
    -> ft8_engine
    -> decoded RX UI
```

Live QMX/UAC follows deterministic replay.

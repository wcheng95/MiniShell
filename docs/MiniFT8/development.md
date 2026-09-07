# MiniFT8-V3 Development

MiniFT8 runs as a MiniShell app. Current work establishes independent RX Audio, TX Audio, and Control resources.

Audio V1 transport for MiniFT8 is 12 kHz/S16/two-channel. MiniShell preserves channel order; MiniFT8 source profiles decide ordinary-audio versus I/Q meaning. `tests/kfs16b12k.wav` is the deterministic reference and is streamed unchanged by the Linux WAV provider.

Control remains independent and generic; QMX/KH1 CAT-frequency TX belongs to Control, QDX modulation belongs to TX Audio, tune composes normal primitives, and device `set_time` is deferred.

Before `ft8_engine` replay or live QMX/UAC, MiniShell is paying H1-H3:

```text
split Linux backend
remove POSIX loader semantics from portable core
split Filesystem private helpers
```

Then evaluate H5 ANSI/CSI split-read robustness cost.

Next slice after housekeeping:

```text
WAV -> Audio ABI -> MiniFT8 channel interpretation/downmix -> ft8_engine -> decoded RX UI
```

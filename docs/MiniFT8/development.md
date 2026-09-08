# MiniFT8-V3 Development

MiniFT8 runs as a MiniShell app with independent RX Audio, TX Audio, and Control resources.

Audio V1 transport for MiniFT8 is 12 kHz/S16/two-channel. MiniShell preserves channel order; MiniFT8 source profiles decide ordinary-audio versus I/Q meaning. `tests/kfs16b12k.wav` is the deterministic reference and is streamed unchanged by the Linux WAV provider.

Control remains independent and generic; QMX/KH1 CAT-frequency TX belongs to Control, QDX modulation belongs to TX Audio, tune composes normal primitives, and device `set_time` is deferred.

The H1-H3 MiniShell housekeeping gate is complete: Linux backend responsibilities are split, POSIX loader semantics no longer leak into portable core, and Filesystem private mechanisms are separated while preserving one service owner.

H5 terminal split-read robustness is now a cost/priority decision rather than a blocking architecture debt.

Next MiniFT8 slice after that decision:

```text
WAV -> Audio ABI -> MiniFT8 channel interpretation/downmix -> ft8_engine -> decoded RX UI
```

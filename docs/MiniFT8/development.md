# MiniFT8-V3 Development

MiniFT8 runs as a MiniShell app with independent RX Audio, TX Audio, and Control resources.

Audio V1 transport for MiniFT8 is 12 kHz/S16/two-channel. MiniShell preserves channel order; MiniFT8 source profiles decide ordinary-audio versus I/Q meaning. `tests/kfs16b12k.wav` is the deterministic reference and is streamed unchanged by the Linux WAV provider.

Control remains independent and generic; QMX/KH1 CAT-frequency TX belongs to Control, QDX modulation belongs to TX Audio, tune composes normal primitives, and device `set_time` is deferred.

The MiniShell H1-H5 housekeeping audit is complete. Linux backend responsibilities are split, POSIX loader semantics do not leak into portable core, Filesystem private mechanisms are separated behind one service owner, and terminal ANSI/CSI/UTF-8 split-read state remains private below Input.

No known MiniShell housekeeping debt blocks the next MiniFT8 slice:

```text
tests/kfs16b12k.wav
    -> MiniShell WAV Audio provider
    -> 12 kHz / S16 / 2-channel Audio ABI
    -> MiniFT8 RX source/profile interpretation
    -> ordinary-audio downmix
    -> ft8_engine
    -> decoded RX UI
```

Keep the boundary strict: MiniShell transports ordered audio channels; MiniFT8 assigns channel meaning and owns all FT8-specific DSP.

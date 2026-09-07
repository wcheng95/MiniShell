# MiniFT8-V3 Development

MiniFT8-V3 is a MiniShell runtime application. Current integrated work covers UI/config/storage plus the Audio transport foundation.

## Independent resources

```text
RX Audio
TX Audio
Control
```

MiniFT8 does not use one monolithic `Radio` object.

## Audio

MiniFT8 V1 requests `12000 Hz / S16 / 2 channels`. MiniShell preserves channel order and does not interpret stereo versus I/Q; source profiles inside MiniFT8 own that meaning.

`tests/kfs16b12k.wav` is the deterministic first RX fixture. The Linux WAV provider streams it unchanged through the Audio ABI.

Live QMX ordinary audio will be converted below MiniShell from its device-native format to the requested 12 kHz/S16/two-channel transport. Decoder-specific select/downmix and any 12 kHz->6 kHz/float conversion stay in MiniFT8.

MiniFT8 owns protocol waveform synthesis for Audio TX; hardware-format conversion stays below MiniShell.

## Control

Control is independent of Audio. Generic concepts include frequency/mode setting, TX begin/end, and dynamic absolute RF-frequency updates. QMX/KH1-style CAT-frequency TX uses Control; QDX modulation uses TX Audio. Tune composes normal primitives. Device `set_time` remains a deferred future capability.

## Housekeeping gate

Before `ft8_engine` replay or live QMX/UAC:

```text
H1 split Linux backend
H2 remove POSIX loader semantics from portable core
H3 split Filesystem private helpers
```

Then evaluate H5 (stateful ANSI/CSI parser) cost.

## Next slice after housekeeping

```text
WAV provider
    -> 12 kHz/S16/two-channel
    -> MiniFT8 source/profile interpretation
    -> select/downmix
    -> ft8_engine
    -> decoded RX UI
```

Audio ABI unit tests and exact WAV replay are already complete.

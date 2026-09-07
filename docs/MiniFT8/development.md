# MiniFT8-V3 Development

MiniFT8-V3 is a MiniShell runtime application. Its current integrated slice covers UI, configuration/scheduler state, Filesystem persistence, clean exit, and the first Audio transport path.

## Resource split

MiniFT8 treats these as independent resources:

```text
RX Audio
TX Audio
Control
```

There is no monolithic `Radio` configuration that implicitly binds all three.

## Audio baseline

MiniFT8 V1 requests:

```text
12000 Hz
signed 16-bit PCM
2 channels
```

MiniShell preserves channel order but does not assign stereo/IQ meaning. MiniFT8 source profiles own channel semantics.

Examples:

```text
ordinary audio -> select/downmix in MiniFT8
I/Q source      -> channel 0 = I, channel 1 = Q in MiniFT8
```

The canonical deterministic fixture is `tests/kfs16b12k.wav`. The Linux WAV provider streams it unchanged through the Audio ABI.

For live QMX ordinary audio:

```text
QMX UAC 48 kHz / 24-bit / 2-channel
    -> MiniShell backend conversion
    -> 12 kHz / S16 / 2-channel
    -> MiniFT8 source profile
```

If the current FT8 decoder prefers 6 kHz mono float, select/downmix and `12 kHz S16 -> 6 kHz float` stay inside MiniFT8/`ft8_engine`.

For audio TX, MiniFT8 owns FT8/FT4 waveform synthesis and writes normalized PCM to MiniShell. Hardware-native conversion stays below the ABI.

## Control baseline

Control remains independent from Audio. The intended generic operations include frequency/mode setting, TX begin/end, and dynamic absolute RF-frequency updates where supported. QMX/KH1-style CAT-frequency TX uses Control; QDX-style modulation uses TX Audio. Tune is composed from normal TX primitives. Device/radio `set_time` remains a valid future capability but is deferred from V1.

## Development rule

For each new capability:

```text
1. define required MiniFT8 behavior
2. identify the MiniFT8 owner
3. check existing MiniShell ABI
4. reuse it if sufficient
5. otherwise define the smallest reusable primitive
6. add MiniShell unit/service tests
7. add MiniFT8 module/integration tests
8. keep platform implementation below MiniShell
```

## Housekeeping gate

The Audio ABI/service and deterministic WAV provider are implemented and tested. Before extending MiniFT8 into `ft8_engine` replay or live QMX/UAC, MiniShell is paying these internal debts:

```text
H1 split Linux backend
H2 remove POSIX loader semantics from portable core
H3 split Filesystem private helpers while preserving one owner
```

After H1-H3, evaluate H5 (stateful ANSI/CSI parsing) before deciding whether to implement it immediately.

## Next vertical slice after housekeeping

```text
tests/kfs16b12k.wav
    -> MiniShell WAV Audio provider
    -> 12 kHz / S16 / 2-channel
    -> MiniFT8 source/profile interpretation
    -> select/downmix
    -> ft8_engine
    -> decoded messages
    -> RX UI
```

Test progression:

```text
Audio ABI unit tests                         DONE
WAV exact two-channel replay                 DONE
MiniFT8 channel interpretation/downmix       NEXT
ft8_engine regression
MiniFT8 app_controller integration
live QMX Audio after deterministic replay
```

## Timing direction

For framed digital audio modes, authoritative UTC identifies slot boundaries and audio sample count is the preferred progression clock inside a slot. OS ticks/scheduling are execution mechanics, not protocol timing.

## Migration policy

Mini-FT8 V2 remains a behavioral/algorithmic reference. Reused code must fit current MiniFT8/MiniShell ownership boundaries. In particular, protocol waveform synthesis stays above MiniShell; Audio providers receive normalized PCM only.

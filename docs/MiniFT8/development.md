# MiniFT8-V3 Development

## Current phase

MiniFT8-V3 is now developed as a MiniShell application rather than as a standalone Linux/ESP-IDF application with its own platform layer.

Current integrated milestone:

```text
MiniShell launch
    -> MiniFT8 30x8 UI
    -> UI actions
    -> config/scheduler state
    -> MiniShell Filesystem persistence
    -> clean app exit
    -> M$>
```

This proves lifecycle, ownership, UI, and persistence before DSP/radio integration.

The next architecture baseline is also now defined:

```text
RX Audio Path
TX Audio Path
Control Path
```

These are independent resources. MiniFT8 does not configure one monolithic `Radio` object that implicitly chooses all three.

## Tests

Two MiniFT8-specific tests are currently part of the MiniShell reference suite.

### `minift8_ui_smoke`

Pure UI-state test. It drives `UiInput` directly and checks `UiFrame`/`AppAction` behavior without a platform or MiniShell backend.

It covers:

- initial RX frame;
- O screen structure;
- mode action generation;
- TX submenu and scheduler controls;
- S screen grouping;
- V/System platform-neutral presentation.

### `linux_minift8`

PTY integration test through the real MiniShell runtime:

```text
M$> MiniFT8
    -> O
    -> FT8 -> FT4
    -> TX submenu
    -> Skip TX1 ON
    -> q
M$>
```

The test then verifies:

- `/flash/MiniFT8/Station.txt` exists;
- mode and Skip TX1 were persisted;
- `Station.txt.tmp` is absent after save;
- relaunch loads FT4/Skip TX1 state;
- exit returns to the MiniShell prompt.

### FT8 audio reference fixture

`tests/kfs16b12k.wav` is the canonical first RX fixture for the new Audio path. Its WAV header is:

```text
12000 Hz
PCM signed 16-bit
2 channels
```

It already matches the MiniFT8 Audio transport baseline, so the WAV provider should validate and stream the two channels without downmixing. Channel meaning belongs above MiniShell: a normal-audio MiniFT8 source profile may select or downmix the channels, while an I/Q profile may interpret channel 0 as I and channel 1 as Q.

## Development rule

For each new capability:

```text
1. define required MiniFT8 behavior
2. identify the MiniFT8 owner
3. check existing MiniShell ABI
4. if sufficient, use it
5. if insufficient, define the smallest reusable MiniShell primitive
6. add MiniShell service/unit tests
7. add MiniFT8 module/integration tests
8. keep platform implementation below MiniShell
```

Do not add broad generic services speculatively.

## Audio baseline

The MiniShell Audio interface should be format-capable, but MiniFT8 V1 requires the default transport format:

```text
12000 Hz
signed 16-bit PCM
2 channels
```

A V1 backend may support only this application-facing format and return `UNSUPPORTED` for other requested formats where conversion is not provided.

MiniShell does not define whether the two channels mean stereo, duplicated mono, or I/Q. It preserves channel ordering and transports the samples. MiniFT8 and its selected RX/TX source profile assign channel meaning.

RX ownership:

```text
source-native audio
    -> MiniShell backend/provider transport-format conversion
    -> 12 kHz / S16 / 2-channel
    -> MiniFT8 source/profile interpretation
```

For the checked-in WAV fixture:

```text
tests/kfs16b12k.wav
12 kHz / S16 / 2-channel
    -> MiniShell WAV Audio provider
    -> unchanged two-channel Audio ABI stream
    -> MiniFT8 normal-audio profile
    -> select/downmix for current FT8 decoder
```

For live QMX ordinary audio:

```text
QMX UAC 48 kHz / 24-bit / 2-channel
    -> MiniShell QMX Audio backend
    -> 12 kHz / S16 / 2-channel
    -> MiniFT8 QMX-AUDIO profile
```

For a future I/Q source:

```text
QMX-IQ / SDR-IQ
    -> MiniShell Audio backend
    -> 12 kHz / S16 / 2-channel
    -> MiniFT8 source profile
       channel 0 = I
       channel 1 = Q
    -> I/Q DSP path
```

If the current FT8 decoder internally prefers 6 kHz mono float, the ordinary-audio select/downmix plus `12 kHz S16 -> 6 kHz float` conversion stays inside MiniFT8/`ft8_engine`.

For audio TX, MiniFT8 synthesizes the FT8/FT4 waveform, applies the selected TX profile's channel mapping, and writes 12 kHz / S16 / 2-channel PCM to MiniShell. For ordinary audio TX the default may be the same waveform in both channels. Hardware-native conversion stays below the ABI.

## Control baseline

The first Control ABI requirement is now concrete rather than speculative.

Conceptual operations:

```text
control_status()
control_get_caps()
control_set_frequency(dial_hz)
control_set_mode(RadioMode)
control_tx_begin(reference_rf_hz)
control_tx_set_frequency(rf_hz)
control_tx_end()
```

Important rules:

- Control accepts generic radio concepts, never FT8 symbols or device-specific CAT commands.
- `control_tx_set_frequency()` means absolute desired RF frequency.
- QMX/KH1-style per-tone/frequency TX uses Control.
- QDX-style modulation uses TX Audio; its Control backend may report no dynamic TX-frequency capability.
- Tune is composed by MiniFT8 from normal TX primitives rather than added as a dedicated Control operation.
- device/radio `set_time` is a valid future capability, but V1 defers it; when added it should reuse MiniShell Time/Location types.

## Next major vertical slice

The next application-driven slice is deterministic FT8 RX:

```text
tests/kfs16b12k.wav
        -> MiniShell WAV Audio provider
        -> 12 kHz / S16 / 2-channel Audio ABI
        -> MiniFT8 normal-audio source profile
        -> select/downmix
        -> app_controller
        -> ft8_engine
        -> decoded messages
        -> UiModel
        -> RX screen
```

Desired test layers:

```text
1. Audio ABI unit tests
2. WAV-provider tests using tests/kfs16b12k.wav, preserving both channels
3. MiniFT8 normal-audio channel-selection/downmix test
4. ft8_engine regression test using the resulting normalized decode stream
5. MiniFT8 integration test through app_controller
6. live QMX Audio backend test after deterministic replay is stable
```

A later I/Q test can reuse the exact same Audio ABI and substitute a MiniFT8 source profile that interprets channel 0/1 as I/Q.

Control can be implemented/tested independently of RX because the three resource paths are deliberately separate.

## Timing direction

For framed digital audio modes:

- authoritative UTC identifies the slot/frame boundary;
- audio sample count is the preferred progression clock inside the slot;
- OS scheduling/ticks are execution mechanics, not protocol timing.

For deterministic WAV replay, replay-start UTC plus samples consumed can provide deterministic virtual UTC progression.

## Migration policy

Mini-FT8 V2 remains a behavioral and algorithmic reference. Proven code may be migrated, but every reused block must fit the current MiniFT8/MiniShell ownership boundaries.

Especially review `ft8_engine`/ft8_lib structure rather than treating old source-file boundaries as architectural requirements.

The old V2 QDX UAC implementation mixes waveform synthesis with USB audio transport. In V3, preserve the proven CPFSK/DDS behavior where useful but move protocol waveform synthesis above MiniShell; the Audio backend should receive normalized PCM only.

## Focused code-reading targets

For the current integrated slice, the most useful files to understand are:

1. `apps/MiniFT8/main/minift8_main.c` — application lifecycle and orchestration boundary.
2. `apps/MiniFT8/main/minishell_ui_adapter.c` — how platform-independent MiniFT8 UI types meet the MiniShell ABI.
3. `apps/MiniFT8/src/storage_service/storage_service.c` — distinction between MiniFT8 file policy and MiniShell filesystem ownership.

For the next RX slice, add the Audio ABI/provider and `ft8_engine` boundary files to this list as they are introduced.

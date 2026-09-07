# MiniFT8-V3 Development

## Current phase

MiniFT8-V3 is developed as a MiniShell application rather than as a standalone Linux/ESP-IDF application with its own platform layer.

The first integrated application milestone remains:

```text
MiniShell launch
    -> MiniFT8 30x8 UI
    -> UI actions
    -> config/scheduler state
    -> MiniShell Filesystem persistence
    -> clean app exit
    -> M$>
```

The first Audio infrastructure milestone is now established:

```text
MiniShell Audio ABI/service core
    -> independent RX/TX stream contracts
    -> 12 kHz / S16 / 2-channel MiniFT8 transport request
    -> Linux deterministic WAV RX provider
    -> runtime-loaded Audio probe
```

RX Audio, TX Audio, and Control remain independent resources. MiniFT8 does not configure one monolithic `Radio` object that implicitly chooses all three.

## Tests

### Existing MiniFT8 tests

`minift8_ui_smoke` is a pure UI-state test. `linux_minift8` is the PTY integration test through the real MiniShell runtime and verifies UI/config/persistence lifecycle.

### Audio ABI unit test

`abi_audio_unit` exercises the public Audio service contract against a fake provider. It covers:

- RX-only, TX-only, and RX+TX capability exposure;
- `struct_size` and invalid argument handling;
- exact-format rejection;
- independent RX and TX lifecycles;
- frame-count semantics;
- exact two-channel ordering;
- finite-source end-of-stream behavior;
- stale handles;
- idempotent start/stop;
- TX abort and RX/TX cleanup at application teardown.

### Linux WAV Audio integration test

`linux_audio` uses the runtime-loaded `audio_probe` application and the checked-in fixture:

```text
tests/kfs16b12k.wav
12000 Hz
PCM signed 16-bit
2 channels
```

The test copies the fixture into the MiniShell logical filesystem and opens it through a logical path. The WAV provider streams the two channels unchanged through the Audio ABI. `audio_probe` hashes every PCM byte and checks the complete frame count. The probe is run twice in one MiniShell session to verify cleanup and reopen behavior.

No wall-clock pacing is used for file replay.

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

The MiniShell Audio interface is format-capable. MiniFT8 V1 requests:

```text
12000 Hz
signed 16-bit PCM
2 channels
```

MiniShell does not define whether the two channels mean stereo, duplicated mono, or I/Q. It preserves channel ordering and transports the samples. MiniFT8 and its selected RX/TX source profile assign channel meaning.

RX ownership:

```text
source-native audio
    -> MiniShell backend/provider transport-format conversion
    -> 12 kHz / S16 / 2-channel
    -> MiniFT8 source/profile interpretation
```

The deterministic host path is now:

```text
tests/kfs16b12k.wav
12 kHz / S16 / 2-channel
    -> MiniShell Linux WAV provider
    -> unchanged two-channel Audio ABI stream
    -> MiniFT8 normal-audio interpretation        # next application step
    -> select/downmix
    -> FT8 decoder
```

For live QMX ordinary audio:

```text
QMX UAC 48 kHz / 24-bit / 2-channel
    -> future MiniShell QMX Audio provider
    -> 12 kHz / S16 / 2-channel
    -> MiniFT8 QMX-AUDIO profile
```

For a future I/Q source:

```text
QMX-IQ / SDR-IQ
    -> MiniShell Audio provider
    -> 12 kHz / S16 / 2-channel
    -> MiniFT8 source profile
       channel 0 = I
       channel 1 = Q
    -> I/Q DSP path
```

If the current FT8 decoder internally prefers 6 kHz mono float, the ordinary-audio select/downmix plus `12 kHz S16 -> 6 kHz float` conversion stays inside MiniFT8/`ft8_engine`.

For audio TX, MiniFT8 synthesizes the FT8/FT4 waveform, applies the selected TX profile's channel mapping, and writes 12 kHz / S16 / 2-channel PCM to MiniShell. For ordinary audio TX the default may be the same waveform in both channels. Hardware-native conversion stays below the ABI.

## Control baseline

The first Control ABI requirement is defined but not yet implemented.

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

With the Audio ABI and deterministic WAV provider established, the next application-driven boundary is inside MiniFT8:

```text
tests/kfs16b12k.wav
        -> MiniShell WAV Audio provider          DONE
        -> 12 kHz / S16 / 2-channel Audio ABI   DONE
        -> MiniFT8 normal-audio interpretation
        -> select/downmix
        -> app_controller
        -> ft8_engine
        -> decoded messages
        -> UiModel
        -> RX screen
```

The next useful test layers are therefore:

```text
1. MiniFT8 normal-audio channel-selection/downmix unit test
2. ft8_engine regression test using the resulting decode stream
3. MiniFT8 integration test through app_controller
4. live QMX Audio provider after deterministic replay/decode is stable
```

A later I/Q test reuses the exact same Audio ABI and substitutes a MiniFT8 source profile that interprets channel 0/1 as I/Q.

Control can be implemented/tested independently because RX Audio, TX Audio, and Control are separate resources.

## Timing direction

For framed digital audio modes:

- authoritative UTC identifies the slot/frame boundary;
- audio sample count is the preferred progression clock inside the slot;
- OS scheduling/ticks are execution mechanics, not protocol timing.

For deterministic WAV replay, replay-start UTC plus samples consumed can provide deterministic virtual UTC progression. File replay should run as fast as the host can process it rather than sleeping to imitate real time.

## Migration policy

Mini-FT8 V2 remains a behavioral and algorithmic reference. Proven code may be migrated, but every reused block must fit the current MiniFT8/MiniShell ownership boundaries.

Especially review `ft8_engine`/ft8_lib structure rather than treating old source-file boundaries as architectural requirements.

The old V2 QDX UAC implementation mixes waveform synthesis with USB audio transport. In V3, preserve the proven CPFSK/DDS behavior where useful but move protocol waveform synthesis above MiniShell; the Audio backend receives normalized PCM only.

## Focused code-reading targets

For the Audio service/provider slice, the highest-value files to read are:

1. `include/minishell/api.h` — public Audio ABI shape and append-only top-level extension.
2. `core/minishell_services/audio_service.c` — stream ownership, lifecycle, capability exposure, and fail-safe teardown.
3. `platform/linux/linux_audio_wav.c` — provider boundary: WAV parsing and exact frame transport without channel interpretation.

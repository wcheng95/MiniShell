# MiniFT8-V3 Architecture

## 1. Invariant

MiniFT8-V3 depends on MiniShell concepts, never directly on a platform implementation.

```text
MiniFT8 application
        |
        v
MiniShell ABI
        |
        v
MiniShell service semantics
        |
        v
platform backend
```

Linux is the current reference implementation, but Linux behavior must not leak into the application core.

## 2. Control-hub model

`app_controller` is the application-level coordinator. Other logical MiniFT8 modules do not coordinate one another behind its back.

```text
                    app_controller
                    /     |      \
                   v      v       v
             ui_shell  config  qso_scheduler
                   |
                   v
          explicit AppAction / UiModel
```

The application edge translates between MiniShell services and MiniFT8-owned types. MiniFT8 never reaches around MiniShell to Linux, NuttX, ESP-IDF, USB, ALSA, UART, I2S, GPIO, or board-specific drivers.

## 3. Current modules and ownership

| Module | Owns |
| --- | --- |
| `minift8_main` | application lifecycle/orchestration loop |
| `minishell_ui_adapter` | translation between MiniShell Display/Input and MiniFT8 UI types |
| `app_controller` | active/requested mode and cross-module sequencing |
| `ui_shell` | Screen/Submenu navigation, UI rendering, UI-local selection state |
| `config_service` | parsed/persisted MiniFT8 configuration values |
| `qso_scheduler` | scheduler-owned runtime settings and QSO/TX policy |
| `storage_service` | MiniFT8 file policy and safe text persistence |
| future `ft8_engine` | FT8-specific DSP, encode/decode, symbol generation, waveform synthesis |
| future radio/control adapter | MiniFT8 use of the MiniShell Control ABI; no CAT syntax or platform transport |

MiniShell remains the owner of application-visible filesystem handles, display semantics, input events, time, memory, audio devices, radio-control transports, and platform resources.

## 4. Independent RX / TX / Control resources

MiniFT8 no longer treats one physical `Radio` selection as the station definition.

A station is composed from three independent logical resources:

```text
RX Audio Path
TX Audio Path
Control Path
```

They may refer to the same physical device, different devices, or be absent independently.

Examples:

```text
QMX normal
    RX      = QMX-AUDIO
    TX      = None
    CONTROL = QMX CAT

QMX I/Q
    RX      = QMX-IQ
    TX      = None
    CONTROL = QMX CAT

QMX with microphone RX
    RX      = Microphone
    TX      = None
    CONTROL = QMX CAT

QDX
    RX      = QDX-AUDIO
    TX      = QDX-AUDIO
    CONTROL = QDX CAT

host test
    RX      = WAV/file source
    TX      = WAV/file sink
    CONTROL = mock
```

A shared physical device may expose more than one child capability, for example USB UAC plus CDC. That physical coordination remains below the independent MiniShell ABIs.

## 5. Audio ABI requirement

The MiniShell Audio ABI is format-capable, but MiniFT8 V1 requires one default application-facing transport format:

```text
sample rate   12000 Hz
sample format signed 16-bit PCM (S16)
channels      2
```

The interface carries an explicit format description so later formats can be appended without redesigning the ABI. V1 implementations may support only `12000 / S16 / 2-channel` at the MiniFT8-facing boundary and return `UNSUPPORTED` for other requested formats where conversion is not provided.

### Channel semantics

MiniShell deliberately does **not** assign domain meaning to the two channels. It transports channel 0 and channel 1 as ordered sample channels only.

Whether those channels mean ordinary stereo audio, duplicated mono, I/Q, or another application-defined pairing is a contract between MiniFT8 and its selected RX/TX source/profile.

Examples:

```text
RX = QMX-AUDIO
    channel 0 / channel 1 = ordinary audio channels
    MiniFT8 may select one channel or downmix before FT8 decode

RX = QMX-IQ
    channel 0 = I
    channel 1 = Q
    MiniFT8 applies the I/Q processing path
```

MiniShell Audio therefore has no `STEREO`, `IQ`, FT8, QMX-IQ, left/right, or complex-sample semantic flag. Those meanings stay above MiniShell.

### RX ownership

The RX contract presented to MiniFT8 is normalized two-channel PCM:

```text
physical/file audio source
        |
        v
MiniShell Audio backend/provider
        |
        | source/native transport-format conversion
        v
12 kHz / S16 / 2-channel
        |
        v
MiniFT8 source/profile interpretation
        |
        +--> ordinary audio -> select/downmix -> FT8 DSP
        |
        +--> I/Q            -> I/Q DSP path
```

Hardware/OS transport-format conversion belongs below the MiniShell Audio ABI. For QMX, a native 48 kHz / 24-bit / 2-channel UAC stream is converted to 12 kHz / S16 / 2-channel while preserving channel ordering. MiniShell does not decide whether those two channels are ordinary audio or I/Q.

The checked-in reference fixture `tests/kfs16b12k.wav` is PCM 12 kHz / S16 / 2-channel, so it already matches the canonical MiniFT8 Audio transport format. The WAV provider validates and streams it without downmixing. Any ordinary-audio downmix belongs inside MiniFT8 after the source/profile has established channel meaning.

Any later conversion needed only by the FT8 implementation remains above MiniShell. For example, if `ft8_engine` continues to process 6 kHz mono float internally, ordinary two-channel audio may be selected/downmixed and converted from `12 kHz S16` to `6 kHz float` inside MiniFT8.

### TX ownership

For audio-based transmission, MiniFT8 owns protocol-specific waveform generation and the application meaning of the two channels:

```text
FT8 message
    -> encoded tone symbols
    -> CPFSK / waveform synthesis
    -> MiniFT8 TX source/profile channel mapping
    -> 12 kHz / S16 / 2-channel PCM
    -> MiniShell TX Audio ABI
    -> backend-native conversion
    -> physical audio device
```

For ordinary audio TX, MiniFT8 may place the same synthesized waveform in both channels unless a selected TX profile specifies another application-level mapping. MiniShell transports the two channels without interpreting them.

MiniShell Audio must not know FT8, FT4, symbol counts, tone spacing, CPFSK, I/Q meaning, or QSO policy. It transports normalized PCM and performs only source/hardware/OS format conversion and buffering.

This also creates a deterministic host test path where the TX Audio backend writes the generated two-channel PCM to a WAV file for independent decoding/regression checks.

## 6. Control ABI requirement

The MiniShell Control ABI exposes generic radio-control concepts, not FT8 concepts.

Conceptual V1 operations are:

```text
control_status()
control_get_caps() -> ControlCaps

control_set_frequency(dial_hz)
control_set_mode(RadioMode)

control_tx_begin(reference_rf_hz)
control_tx_set_frequency(rf_hz)
control_tx_end()
```

The exact C table/signatures must follow the normal MiniShell append-only ABI rules, but these semantics are the application requirement.

Conceptual capabilities are:

```text
ControlCaps
    can_set_frequency
    can_set_mode
    can_tx_begin_end
    can_tx_frequency_control
    tx_frequency_min_interval_us
```

`control_tx_set_frequency()` uses the desired absolute RF frequency. MiniFT8 must not send a device-specific tone index, `TA` command value, `FO` index, or FT8 symbol through the ABI.

MiniFT8 owns:

- slot/frame timing;
- symbol timing;
- TX audio offset;
- the mapping from FT8/FT4 symbols to desired RF frequency;
- tune policy.

MiniShell/Control backends own:

- CAT syntax;
- physical control transport;
- device-specific TX/RX sequencing;
- caching/retry details;
- conversion from absolute requested RF frequency to the device mechanism;
- required restoration of radio state after TX.

Examples:

```text
QMX backend
    desired RF frequency
        -> relative tone from configured dial
        -> QMX TA command

KH1 backend
    desired RF frequency
        -> KH1 FA/FO realization

QDX backend
    can_tx_frequency_control = false
    modulation comes from the independent TX Audio path
```

### Tune

Tune is application policy, not a dedicated MiniShell Control primitive.

MiniFT8 can realize tune by:

- beginning Control TX and holding one RF frequency on a control-frequency radio; or
- beginning Control TX/PTT and sending a constant PCM tone through TX Audio on an audio-modulated radio.

### Device time

Setting a radio/device clock is a legitimate future Control capability, but it is deferred from V1. When added, it should reuse the canonical MiniShell Time/Location representation rather than inventing a second radio-only hour/minute/second type.

## 7. TX realization paths

After `ft8_engine` produces the protocol TX signal plan, `app_controller` chooses a realization from available capabilities.

### Control-frequency TX

Typical QMX-style path:

```text
FT8 symbols
    -> MiniFT8 timing / desired RF frequency
    -> MiniShell Control ABI
    -> QMX CAT
```

No TX Audio resource is required.

### Audio TX

Typical QDX-style path:

```text
FT8 symbols
    -> MiniFT8 waveform synthesis
    -> MiniFT8 channel mapping
    -> 12 kHz / S16 / 2-channel
    -> MiniShell TX Audio ABI
    -> QDX UAC
```

Control remains independently responsible for TX/RX state, frequency, and other radio control as supported.

The TX mechanism is selected from capabilities, not from a monolithic physical-radio type.

## 8. Storage boundary

`storage_service` does not own the filesystem. It owns MiniFT8 file policy:

- MiniFT8 data directory;
- Station configuration naming;
- complete text reads/writes;
- safe temporary-file save sequence.

MiniShell Filesystem owns logical paths, file handles, namespace behavior, quota policy, and backend/native file operations.

This distinction is important: `/flash/MiniFT8/Station.txt` is MiniFT8 policy; how `/flash` maps to Linux, NuttX, FATFS, or another backend is MiniShell policy.

## 9. UI boundary

The UI uses a fixed logical frame for the current milestone:

```text
30 columns x 8 rows
```

`ui_shell` does not know terminal dimensions, ANSI sequences, ncurses, touch hardware, or keyboard scan codes. It sees only `UiInput` and produces `UiFrame`.

The adapter requires MiniShell text Display >= 30x8 and key Input. Unsupported platforms fail cleanly at application startup rather than bypassing MiniShell.

## 10. Mode ownership

`Mode` means operating protocol/application behavior:

```text
FT8 / FT4 / RTTY / CW / future
```

Current canonical mode is owned by `app_controller`. `ui_shell` receives it through `UiModel`; it does not own duplicate application mode state.

Future mode-specific engines remain subordinate to the same controller. FT8-specific DSP belongs in `ft8_engine`, not in Display, Audio, Control, or `ui_shell`.

## 11. Next service boundary

The Audio and Control requirements above are now architecturally defined. They are not yet claims that the corresponding MiniShell services are implemented.

The next RX vertical slice is:

```text
tests/kfs16b12k.wav
12 kHz / S16 / 2-channel
        -> MiniShell WAV Audio provider
        -> 12 kHz / S16 / 2-channel Audio ABI
        -> MiniFT8 source/profile interpretation
        -> ordinary-audio downmix for current FT8 decode
        -> app_controller / ft8_engine
        -> decoded messages
        -> UiModel
        -> RX screen
```

A future I/Q source uses the same MiniShell Audio format:

```text
QMX-IQ or another I/Q source
        -> MiniShell Audio backend
        -> 12 kHz / S16 / 2-channel
        -> MiniFT8 source profile defines channel 0 = I, channel 1 = Q
        -> I/Q DSP path
```

The later live QMX ordinary-audio path also produces the same application-facing transport format:

```text
QMX 48 kHz / 24-bit / 2-channel UAC
        -> MiniShell QMX Audio backend
        -> 12 kHz / S16 / 2-channel
        -> MiniFT8 QMX-AUDIO profile
```

Control can be implemented and tested independently because RX Audio, TX Audio, and Control are separate resources.

## 12. Design rules

1. Start from required MiniFT8 behavior, then ask whether the existing MiniShell ABI expresses it.
2. Add a MiniShell primitive only when it is generally reusable and justified by a real application requirement.
3. RX Audio, TX Audio, and Control are independent logical resources; physical device identity must not couple them at the application boundary.
4. MiniFT8 owns protocol-specific DSP, modulation, timing, symbol meaning, TX waveform synthesis, and channel semantics such as ordinary audio versus I/Q.
5. MiniShell owns platform/hardware transport, normalized two-channel PCM delivery, source/hardware transport-format conversion, and radio-specific control realization; it does not interpret channel meaning.
6. Keep one authoritative owner for mutable state/resource policy.
7. Keep platform implementation below MiniShell.
8. Prefer synchronous explicit calls until concurrency is actually required.
9. Keep modules small enough to explain and test independently.
10. Reuse proven V2 code for behavior/algorithms, but do not inherit old structural coupling automatically.
11. C/C++ choice remains pragmatic; boundaries and ownership matter more than language purity.

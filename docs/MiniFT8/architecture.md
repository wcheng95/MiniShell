# MiniFT8-V3 Architecture

## 1. Invariant

MiniFT8-V3 depends on MiniShell concepts, never directly on a platform implementation.

```text
MiniFT8 application
        |
        v
MiniShell public ABI
        |
        v
MiniShell service semantics
        |
        v
private backend/provider boundary
        |
        v
Linux / NuttX / ESP-IDF / hardware / mocks
```

Linux is the current reference implementation, but Linux behavior must not leak into the application core.

## 2. Application control-hub model

`app_controller` is the owner of MiniFT8 domain coordination. Other logical MiniFT8 modules do not coordinate one another behind its back.

```text
MiniShell edge adapters
        |
        v
  app_controller
   /    |     \
  v     v      v
config  qso   future ft8_engine
        scheduler
   
UiModel / AppAction
        |
        v
     ui_shell
```

`minift8_main` owns the foreground application loop and lifecycle only. It wires the edge adapters, controller, and UI together; it must not become a second owner of mode, scheduler, DSP, radio, or configuration state.

The application edge translates between MiniShell service types and MiniFT8-owned types. MiniFT8 never reaches around MiniShell to Linux, NuttX, ESP-IDF, USB, ALSA, UART, I2S, GPIO, or board-specific drivers.

## 3. Current modules and ownership

| Module | Owns |
| --- | --- |
| `minift8_main` | foreground application lifecycle and top-level call sequence |
| `minishell_ui_adapter` | translation between MiniShell Display/Input and MiniFT8 `UiFrame`/`UiInput` |
| `app_controller` | active/requested mode and cross-module domain sequencing |
| `ui_shell` | Screen/Submenu navigation, rendering, UI-local selection state |
| `config_service` | parsed/persisted MiniFT8 configuration values |
| `qso_scheduler` | scheduler-owned runtime settings and QSO/TX policy |
| `storage_service` | MiniFT8 file policy and safe text persistence through MiniShell Filesystem |
| future MiniFT8 RX-audio edge | opening/reading MiniShell RX Audio and assigning source/profile channel meaning |
| future `ft8_engine` | FT8-specific DSP, encode/decode, symbol generation, waveform synthesis |
| future MiniFT8 Control edge | use of the future MiniShell Control ABI; no CAT syntax or platform transport |

MiniShell owns application-visible filesystem handles, display semantics, input events, time/location, memory, audio stream lifecycle, and platform resources. A future MiniShell Control service will likewise own application-visible radio-control transport/resource lifecycle.

## 4. Independent RX Audio / TX Audio / Control resources

MiniFT8 does not use one physical `Radio` selection as the station definition.

A station is composed from three independent logical resources:

```text
RX Audio
TX Audio
Control
```

They may refer to the same physical device, different devices, or be absent independently.

Examples:

```text
QMX normal
    RX Audio = QMX-AUDIO
    TX Audio = None
    Control  = QMX CAT

QMX I/Q
    RX Audio = QMX-IQ
    TX Audio = None
    Control  = QMX CAT

QMX with microphone RX
    RX Audio = Microphone
    TX Audio = None
    Control  = QMX CAT

QDX
    RX Audio = QDX-AUDIO
    TX Audio = QDX-AUDIO
    Control  = QDX CAT

host test
    RX Audio = WAV/file source
    TX Audio = WAV/file sink
    Control  = mock
```

A shared physical device may expose several child capabilities, for example USB UAC plus CDC. Physical coordination belongs below the independent MiniShell ABIs and must not recreate a monolithic application-side radio object.

## 5. Audio boundary

MiniShell Audio V1 is implemented and format-capable. MiniFT8's current requested application-facing transport format is:

```text
sample rate   12000 Hz
sample format signed 16-bit PCM (S16)
channels      2
```

### 5.1 Transport versus channel meaning

MiniShell transports channel 0 and channel 1 in order. It deliberately does **not** assign domain meaning to them.

The meaning is a MiniFT8 source/profile contract:

```text
RX = ordinary audio source/profile
    channel 0 / channel 1 = ordinary audio channels
    MiniFT8 selects or downmixs before FT8 DSP

RX = I/Q source/profile
    channel 0 = I
    channel 1 = Q
    MiniFT8 uses an I/Q processing path
```

MiniShell Audio therefore has no `STEREO`, `IQ`, FT8, QMX-IQ, left/right, or complex-sample semantic flag.

This distinction is a hard boundary:

```text
what bytes/frames are transported     MiniShell
what the two channels mean            MiniFT8
```

### 5.2 RX ownership

```text
physical/file source
        |
        v
MiniShell Audio backend/provider
        |
        | native transport conversion only
        v
12 kHz / S16 / 2-channel
        |
        v
MiniFT8 RX source/profile interpretation
        |
        +--> ordinary audio -> select/downmix -> FT8 DSP
        |
        +--> I/Q            -> I/Q DSP path
```

Hardware/OS transport conversion belongs below the MiniShell Audio ABI. For example, a future QMX provider may convert native 48 kHz / 24-bit / 2-channel UAC to 12 kHz / S16 / 2-channel while preserving channel order.

The checked-in `tests/kfs16b12k.wav` is PCM 12 kHz / S16 / 2-channel. The Linux WAV provider validates it and streams it unchanged. It does not downmix or interpret the channels.

If the current FT8 decoder requires 6 kHz mono float internally, the 12 kHz two-channel -> selected/downmixed -> 6 kHz float conversion belongs inside MiniFT8, not MiniShell.

### 5.3 TX ownership

For audio-based TX, MiniFT8 owns protocol waveform synthesis and channel mapping:

```text
FT8 message
    -> encoded symbols
    -> CPFSK / waveform synthesis
    -> MiniFT8 TX profile channel mapping
    -> 12 kHz / S16 / 2-channel
    -> MiniShell TX Audio
    -> backend-native conversion
    -> physical device
```

MiniShell must not know FT8/FT4 symbol counts, tone spacing, CPFSK, I/Q meaning, or QSO policy.

## 6. Control boundary

Control is architecturally defined but is **not yet an implemented MiniShell public service**.

The future MiniShell Control ABI exposes generic radio-control concepts, not FT8 concepts.

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

Conceptual capabilities are:

```text
ControlCaps
    can_set_frequency
    can_set_mode
    can_tx_begin_end
    can_tx_frequency_control
    tx_frequency_min_interval_us
```

`control_tx_set_frequency()` uses desired absolute RF frequency. MiniFT8 must never send a device-specific `TA` value, `FO` index, CAT command string, or FT8 symbol through the ABI.

MiniFT8 owns:

- FT8/FT4 slot and symbol timing;
- TX audio offset;
- mapping protocol symbols to desired RF frequency;
- tune policy.

A MiniShell Control backend will own:

- CAT syntax;
- physical control transport;
- device-specific RX/TX sequencing;
- retry/caching details;
- translating requested RF frequency into the device mechanism;
- required restoration of radio state after TX.

### Tune

Tune is application policy, not a dedicated MiniShell Control primitive.

### Device time

Setting a radio/device clock is a legitimate future Control capability but remains deferred. When added, it should reuse canonical MiniShell Time/Location data rather than introduce a radio-only time representation.

## 7. TX realization remains capability-driven

After `ft8_engine` produces a TX signal plan, `app_controller` chooses a realization based on capabilities.

Control-frequency radio:

```text
FT8 symbols
    -> MiniFT8 desired RF frequency/timing
    -> MiniShell Control
    -> device control backend
```

Audio-modulated radio:

```text
FT8 symbols
    -> MiniFT8 waveform synthesis
    -> 12 kHz / S16 / 2-channel
    -> MiniShell TX Audio
    -> audio backend
```

The decision is based on independent capabilities, never a switch on one monolithic physical-radio identity.

## 8. Storage boundary

`storage_service` owns MiniFT8 file **policy**, not the filesystem:

- MiniFT8 data directory;
- Station configuration naming;
- complete text reads/writes;
- safe temporary-file save sequence.

MiniShell Filesystem owns:

- logical namespace and paths;
- file/directory handles;
- lifecycle cleanup;
- namespace semantics;
- quota policy;
- platform/native file operations through its backend.

Thus `/flash/MiniFT8/Station.txt` is MiniFT8 policy, while how `/flash` maps to Linux, NuttX, FATFS, or another backend is MiniShell policy.

## 9. UI boundary

The current logical UI frame is:

```text
30 columns x 8 rows
```

`ui_shell` sees only MiniFT8-owned `UiModel`, `UiInput`, `UiFrame`, and `AppAction`. It does not know terminal dimensions, ANSI sequences, ncurses, touch hardware, or keyboard scan codes.

```text
MiniShell key event
    -> minishell_ui_adapter
    -> UiInput
    -> ui_shell
    -> AppAction
    -> app_controller

app_controller
    -> UiModel
    -> ui_shell
    -> UiFrame
    -> minishell_ui_adapter
    -> MiniShell Display
```

The Settings and Status vocabulary mirrors the architecture directly:

```text
RX Audio
TX Audio
Control
```

There is no UI-level monolithic `Radio` resource.

## 10. Mode ownership

`Mode` means operating protocol/application behavior:

```text
FT8 / FT4 / RTTY / CW / future
```

Canonical mode is owned by `app_controller`. `ui_shell` receives it in `UiModel`; it does not own duplicate application mode state.

Future mode-specific engines remain subordinate to the same controller. FT8-specific DSP belongs in `ft8_engine`, not Display, Audio, Control, or `ui_shell`.

## 11. Current implementation boundary and next slice

Implemented MiniShell side:

```text
Audio public ABI
Audio resident service/lifecycle
Linux deterministic WAV RX provider
12 kHz / S16 / 2-channel reference fixture
```

Not yet implemented/integrated:

```text
MiniFT8 RX Audio consumer/source-profile layer
ft8_engine replay/decode path
MiniShell Control public service
live QMX/UAC provider
TX realization
```

The next vertical slice is therefore entirely above the already-established Audio transport boundary until `ft8_engine` receives samples:

```text
tests/kfs16b12k.wav
    -> MiniShell WAV provider
    -> MiniShell Audio ABI: 12 kHz / S16 / 2-channel
    -> MiniFT8 RX source/profile interpretation
    -> ordinary-audio select/downmix
    -> ft8_engine
    -> decoded messages
    -> app_controller
    -> UiModel
    -> RX screen
```

A later I/Q source uses the same Audio ABI but selects the MiniFT8 I/Q branch instead of ordinary-audio downmix.

## 12. Boundary rules

1. Application code depends on MiniShell public ABI only; no platform implementation header or OS/device API may enter MiniFT8.
2. `app_controller` is the MiniFT8 domain coordinator; edge adapters and `minift8_main` must not become competing owners of application state.
3. Logical MiniFT8 modules communicate through explicit MiniFT8-owned data/contracts rather than calling one another opportunistically.
4. RX Audio, TX Audio, and Control are independent resources; physical device identity never couples them at the application boundary.
5. MiniShell Audio owns transport, stream lifecycle, buffering, and native-format conversion; MiniFT8 owns channel meaning and DSP conversion.
6. MiniFT8 owns FT8/FT4 protocol semantics, timing, modulation, waveform synthesis, and QSO policy.
7. MiniShell Control, when implemented, owns generic device/radio control realization but never FT8 symbols or tune policy.
8. `storage_service` owns MiniFT8 file policy; MiniShell Filesystem remains the filesystem/resource owner.
9. Mocks and simulations stay below MiniShell unless the thing being tested is a pure MiniFT8 module with an explicit MiniFT8-level test interface.
10. Keep one authoritative owner for mutable state/resource policy.
11. Prefer synchronous explicit calls until concurrency is proven necessary.
12. Keep modules small enough to explain and test independently; split private implementation without splitting semantic ownership.
13. Reuse proven V2 behavior/algorithms, but never import old structural coupling automatically.

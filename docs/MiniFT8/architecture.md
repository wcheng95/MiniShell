# MiniFT8-V3 Architecture

## 1. Invariant

MiniFT8-V3 depends on MiniShell concepts, never directly on a platform implementation. MiniFT8 is the project/design name; its current MiniShell runtime application is `ft8`.

```text
ft8 application
        |
        v
MiniShell public API
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

Linux remains the deterministic reference/regression implementation, and Cardputer ADV is now a proven embedded implementation for live QMX USB-host RX. Neither backend may leak platform behavior into the application core.

### 1.1 Protocol applications

Protocol selection is a MiniShell application-lifecycle decision, not mutable
state inside `ft8`.

The portable QRP radio set is intentionally closed at five applications/modes:

```text
ft8       FT8
minicw    CW
js8chat   JS8 Normal
rtty      classic amateur RTTY
sstv      Robot 36
```

The `ft8` application therefore owns FT8 behavior only. **FT4 is intentionally
out of scope**, not a future MiniFT8 mode or future MiniShell application.
The five radio applications may share implementation modules where real
commonality appears, but no umbrella protocol-mode owner is introduced
speculatively.

### 1.2 MiniFT8 application profiles

MiniFT8 has an application-side **profile** layer for resource and presentation policy. A profile is not a MiniShell backend and does not identify the physical platform on which the application is running.

```text
MiniFT8 profile
        |
        v
ft8 application/core
        |
        v
MiniShell public API
        |
        v
MiniShell backend/provider
        |
        v
hardware / OS
```

MiniShell tells MiniFT8 what services and capabilities the environment provides. The MiniFT8 profile decides how the FT8 application uses those resources, for example display line count, history depth, decoder/resource limits, or waterfall dimensions.

Profiles must not contain FT8 protocol behavior, QSO policy, autoseq logic, scheduler algorithms, logging semantics, or platform-driver code. Those remain shared `ft8` application logic.

The initial profiles are:

```text
ADV
    captures the current MiniFT8-V2/Cardputer ADV behavior and resource assumptions

DESKTOP
    initial pc-1/Linux development profile with larger resource and display allowances
```

Profile and backend are intentionally independent. In particular, pc-1/Linux must be able to run both:

```text
Linux MiniShell + DESKTOP profile
Linux MiniShell + ADV profile
```

Running the ADV profile on Linux is a first-class regression/test configuration. Profile selection should therefore be runtime/application configuration where practical, rather than compile-time platform branching such as `#ifdef CARDPUTER_ADV` in shared FT8 logic.

Additional profiles such as PaperS3 or Tab5 should be introduced only when those targets are actively developed and real differences justify new profile fields.

## 2. Application control-hub model

`app_controller` owns FT8-domain coordination. Other logical MiniFT8 modules do not coordinate one another behind its back.

```text
MiniShell edge adapters -> app_controller
                              |-> config_service / storage_service / log_service
                              |-> auto_seq / tx_lifecycle
                              |-> rx_audio_adapter / rx_frontend / rx_slot_framer
                              |-> ft8_engine / rx_result_builder
                              `-> UiModel / AppAction <-> ui_shell
```

`ft8_main` owns the foreground application loop and lifecycle only. It wires the edge adapters, controller, and UI together; it must not become a second owner of scheduler, DSP, radio/control, or configuration state.

The application edge translates between MiniShell service types and MiniFT8-owned types. MiniFT8 never reaches around MiniShell to Linux, NuttX, ESP-IDF, USB, ALSA, UART, I2S, GPIO, or board-specific drivers.

## 3. Current modules and ownership

| Module | Owns |
| --- | --- |
| `ft8_main` | foreground application lifecycle and top-level call sequence |
| `ft8_ui_adapter` | translation between MiniShell Display/Input and MiniFT8 `UiFrame`/`UiInput` |
| `app_controller` | cross-module FT8-domain sequencing |
| `ui_shell` | Screen/Submenu navigation, rendering, UI-local selection state |
| `config_service` | parsed/persisted FT8 configuration values |
| `auto_seq` | pure QSO sequencing, queue, eligibility, typed log events and per-format ACK state |
| `tx_lifecycle` | pure TX-slot/parity/lifecycle eligibility |
| `tx_encoder` | pure AutoSeq intent -> canonical FT8 payload and immutable 79-tone transmit plan |
| `rx_audio_adapter` | MiniShell Audio RX edge and application stream lifecycle |
| `rx_frontend` | canonical 12 kHz S16 stereo to 6 kHz mono float conversion |
| `rx_slot_framer` | FT8 slot/sample progression and block framing |
| `ft8_engine` | FT8 DSP/protocol decode and hash state |
| `rx_result_builder` | engine results to factual `RxBatch` entries |
| `storage_service` | configuration/text persistence helpers through MiniShell Filesystem |
| `log_service` | ADIF/Cabrillo serialization, date/time/frequency/path policy and copy-on-write file mutation through injected MiniShell APIs |
| `presentation_profile` | presentation/layout profile facts |
| `radio_control` | MiniFT8 control-only radio abstraction and lifecycle |
| `radio_qmx` | QMX CAT command construction/ordering over MiniShell serial/CDC |

MiniShell owns application-visible filesystem handles, display semantics, input events, time/location, memory, audio stream lifecycle, serial/CDC byte-stream lifecycle, and platform resources. Radio/CAT protocol semantics remain application-owned.

## 4. Independent RX Audio / TX Audio / Control resources

MiniFT8 does not use one physical `Radio` selection as the station definition.

A station is composed from three independent logical resources:

```text
RX Audio
TX Audio
Control transport
```

For QMX, the control transport is USB CDC/ACM. MiniShell owns that byte stream;
MiniFT8's radio adapter owns QMX CAT command syntax and radio semantics.

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

A shared physical device may expose several child capabilities, for example USB UAC plus CDC. Physical coordination belongs below the independent MiniShell APIs and must not recreate a monolithic application-side radio object.

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
    MiniFT8 selects or downmixes before FT8 DSP

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

Hardware/OS transport conversion belongs below the MiniShell Audio API. The checked-in `tests/kfs16b12k.wav` is PCM 12 kHz / S16 / 2-channel. The Linux WAV provider validates it and streams it unchanged.

If the FT8 decoder requires a different internal representation, such as 6 kHz mono float, that DSP conversion belongs inside MiniFT8, not MiniShell.

### Live RX capture/decode ownership (T081)

The controller schedules each live slot from UTC: producer reset at -1.60 s,
anchor at UTC, and decode submission at +12.64 s. Audio discontinuities may reset
frontend conversion but do not cancel decode or restart this schedule.

The producer owns monitor/FFT state and slot anchors. Its resets never clear a
decode job. The ADV core-1 worker owns candidate search, noise estimation,
LDPC/CRC, message decode and hash aging based on accepted decode-slot progression.
The intentional shared input is the linear waterfall view; a late reader may see
later producer writes without affecting capture timing.

The single `protocol_messages[50]` buffer remains owned by the completed slot
until the controller builds/publishes its `RxBatch`. RUNNING or unconsumed READY
at the following decode trigger is an invariant fault, distinguished by prior
slot and elapsed time/result age. No result queue, extra message buffer or silent
slot-dropping policy is introduced. Intentional physical-TX cancellation and
resume remain separate from live transport discontinuities.

### 5.3 TX ownership

For future physical audio-based TX, MiniFT8 will own protocol waveform synthesis and channel mapping:

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

MiniShell must not know FT8 symbol counts, tone spacing, CPFSK, I/Q meaning, or QSO policy.

## 6. Control transport and CAT ownership

MiniShell owns the **transport**, not the radio protocol.

For QMX, MiniShell exposes an application-visible serial/CDC byte stream. The
platform/backend owns:

- opening and closing the CDC/serial endpoint;
- platform resource ownership and cleanup;
- raw byte reads/writes and timeout semantics;
- Linux tty/CDC mechanics or ADV USB-host CDC mechanics;
- mapping an opaque endpoint string to the platform transport.

MiniShell does **not** know CAT command syntax, FT8, QMX mode numbers, VFO policy,
tone-offset commands, or TX sequencing.

MiniFT8 owns a radio-control adapter above that byte stream:

```text
app_controller / FT8 policy
        |
        v
MiniFT8 radio_control
        |
        v
MiniFT8 radio_qmx
        |  owns: MD6; FR0; FT0; FA...; TX; RX; TA...;
        v
MiniShell Serial/CDC byte stream
        |
        v
Linux tty / ADV USB-host CDC
```

This follows the same boundary as Audio:

```text
transport bytes/lifecycle     MiniShell
meaning/protocol of bytes     MiniFT8 radio adapter
```

The application-side radio abstraction is **control-only**. It does not own RX
Audio or TX Audio and must not recreate a monolithic physical-radio object.

MiniFT8 owns:

- selected-band/radio intent;
- FT8 slot and symbol timing;
- TX audio offset;
- mapping FT8 symbols/tone offsets to radio-control requests;
- QMX CAT command construction and ordering;
- tune policy.

The MiniShell serial/CDC service owns only the byte transport and resource lifecycle.

## 7. TX realization remains capability-driven

Linux/QMX physical TX is implemented and hardware validated. `app_controller`
owns the slot-anchored physical lifecycle after AutoSeq supplies semantic intent
and `tx_encoder` produces an immutable 79-tone `Ft8TxPlan`.

Current QMX control-frequency path:

```text
AutoSeq TxIntent
    -> tx_encoder / Ft8TxPlan
    -> app_controller absolute symbol scheduler
    -> MiniFT8 radio_control / radio_qmx
       MD6; TX;
       TAxxxx.xx; per changed symbol tone
       RX;
    -> MiniShell Serial/CDC
    -> QMX
```

RX capture is paused/restarted around physical TX and re-anchored to UTC/sample
timing before subsequent decode. Failed/uncertain TX attempts conservatively
attempt QMX `RX;` restoration and do not consume semantic completion.

Audio-modulated path remains future capability:

```text
FT8 symbols
    -> MiniFT8 waveform synthesis
    -> 12 kHz / S16 / 2-channel
    -> MiniShell TX Audio
    -> audio backend
```

RX Audio, TX Audio, and control transport remain independent resources even when
they terminate on the same physical QMX.

## 8. Storage boundary

`storage_service` provides configuration/text persistence helpers; it does not own the filesystem:

- ensuring the selected application directory exists;
- complete text reads/writes;
- safe temporary-file save sequence.

MiniShell Filesystem owns logical namespace and paths, handles, lifecycle cleanup, quota policy, and platform/native file operations through its backend.

`ft8_main` selects `/flash/ft8/station.txt` by default, and `config_service` owns configuration parsing/serialization. Thus this path is MiniFT8 policy, while how `/flash` maps to Linux, LittleFS, NuttX, or another backend is MiniShell policy. The V2-compatible `RxTxLog` traffic log is enabled by default in V3 (configurable
with `rxtx_log` in `station.txt`) and is persisted by `log_service` as
`/flash/ft8/RTYYMMDD.txt`. ADIF/Cabrillo/RxTxLog path and serialization policy
belong to `log_service`, using MiniShell Filesystem and Time/Location only.

AutoSeq owns pure log eligibility/events and per-format ACK state. At TX start,
`app_controller` snapshots station/QSO facts, calls `log_service`, and ACKs only
successful persistence independently for ADIF and Cabrillo. `log_service` owns
serialization, UTC/date/frequency/path policy and copy-on-write mutation through
injected Filesystem and Time/Location APIs. It syncs/closes a temporary file before
rename commits the new log; known pre-commit failures leave the final log unchanged.

The FT8-only configuration does not persist a protocol `mode=` value. Protocol identity comes from the application being launched.

## 9. UI boundary

The ADV profile logical text frame is:

```text
20 columns x 7 rows
```

On Cardputer ADV this maps to the 240x135 display using the MiniShell ADV text backend. The `ft8` ADV profile uses row 0 contextually for status/temporary help/countdown text and rows 1..6 for main FT8 content. This row meaning belongs to the application profile, not MiniShell Display.

The 20x7 geometry is an **ADV profile** choice, not a MiniShell API or universal MiniFT8 architectural limit. The DESKTOP profile may use a larger frame while using the same application logic and MiniShell Display API.

`ui_shell` sees only MiniFT8-owned `UiModel`, `UiInput`, `UiFrame`, and `AppAction`. It does not know terminal dimensions, ANSI sequences, ncurses, touch hardware, or keyboard scan codes.

```text
MiniShell key event
    -> ft8_ui_adapter
    -> UiInput
    -> ui_shell
    -> AppAction
    -> app_controller

app_controller
    -> UiModel
    -> ui_shell
    -> UiFrame
    -> ft8_ui_adapter
    -> MiniShell Display
```

The Settings and Status vocabulary mirrors the architecture directly:

```text
Protocol: FT8    fixed identity, not a selector
RX Audio
TX Audio
Control
```

There is no UI-level monolithic `Radio` resource and no in-app protocol-mode selector.

## 10. Protocol ownership

The `ft8` application is FT8. There is no canonical mutable `Mode` state in `app_controller`, `UiModel`, or `station.txt`.

Switching protocols means returning to MiniShell and launching another application:

```text
M$> ft8
...
q
M$> js8chat
```

FT4 is not part of the MiniShell portable-radio scope. Other members of the
closed five-mode set may reuse proven modules where the interfaces genuinely
match, but FT8-specific DSP remains owned by `ft8_engine` inside the FT8
application unless/until a justified shared lower-level library is extracted.

## 11. Current implementation boundary

Implemented production baseline:

```text
MiniShell Audio RX API/service
Linux deterministic WAV RX provider
Linux live ALSA/QMX capture worker and ring
ADV ESP-IDF USB-host/QMX UAC capture worker and canonical ring
rx_audio_adapter -> rx_frontend -> rx_slot_framer
    -> ft8_engine -> rx_result_builder / RxBatch
continuous multi-slot live RX with V2-compatible 12.64-second decoding
real on-air ADV/QMX FT8 decode at ESP32-S3 240 MHz
ADV engine profile: time_osr=2, freq_osr=1
AutoSeq AS-0..AS-8
pure FT8 TX encoder and immutable 79-tone plans
Linux/QMX slot-anchored physical CAT TX with RX recovery
V2-compatible RxTxLog plus ADIF and Field Day Cabrillo logging through log_service
completed real two-way Linux/QMX QSO
```

The RX pipeline transports 12 kHz S16 stereo through MiniShell, converts to 6 kHz
mono float in `rx_frontend`, and frames blocks/slots in `rx_slot_framer` before
engine decoding. `app_controller` projects results into AutoSeq and UiModel.
Capture continues in a platform-owned worker while synchronous decoding runs:
ALSA on Linux and ESP-IDF USB-host/UAC on ADV.

The ADV `freq_osr=2` comparison is not part of the production profile: it remained
alive but consumed roughly 103 KiB more application memory and did not decode during
the hardware comparison. The accepted ADV profile remains `freq_osr=1`.

QMX receive CAT synchronization, CAT TX primitives, the pure FT8 79-tone encoder,
and real slot-anchored physical FT8 transmission are implemented. T022-T024
validated this path on pc-1/QMX; WinBook/TW700 also runs the same pc-1-built
Linux binaries with QMX RX and CAT/TX. T026 adds a temporary pinned-V2
compatibility exception for the ambiguous exact string `RR73`: a standard GRID
field containing `RR73` is classified as terminal TX4 before ordinary grid
classification. Because `RR73` is also a legitimate Maidenhead locator, permanent
disambiguation remains deferred.

The accepted ownership remains unchanged: TxLifecycle, AutoSeq intent, Ft8TxPlan,
and QMX CAT semantics stay inside MiniFT8 while MiniShell owns transport/resource
lifecycle. A future I/Q source would use the same Audio API with an
application-owned I/Q processing path.

## 12. Boundary rules

1. Application code depends on MiniShell public API only; no platform implementation header or OS/device API may enter MiniFT8.
2. `app_controller` is the FT8-domain coordinator; edge adapters and `ft8_main` must not become competing owners of application state.
3. Logical MiniFT8 modules communicate through explicit MiniFT8-owned data/contracts rather than calling one another opportunistically.
4. RX Audio, TX Audio, and Control are independent resources; physical device identity never couples them at the application boundary.
5. MiniShell Audio owns transport, stream lifecycle, buffering, and native-format conversion; MiniFT8 owns channel meaning and DSP conversion.
6. The `ft8` application owns FT8 protocol semantics, timing, modulation, waveform synthesis, and QSO policy. Other protocols are separate applications.
7. MiniShell owns serial/CDC transport bytes and lifecycle only; MiniFT8 radio adapters own CAT syntax and radio-control semantics.
8. `storage_service` owns config/text persistence helpers and `log_service` owns log serialization/persistence policy; MiniShell Filesystem remains the filesystem/resource owner.
9. Mocks and simulations stay below MiniShell unless the thing being tested is a pure MiniFT8 module with an explicit MiniFT8-level test interface.
10. Keep one authoritative owner for mutable state/resource policy.
11. Prefer synchronous explicit calls until concurrency is proven necessary.
12. Keep modules small enough to explain and test independently; split private implementation without splitting semantic ownership.
13. Reuse proven V2 behavior/algorithms, but never import old structural coupling automatically.
14. MiniFT8 profiles are application policy above the MiniShell API; profile and backend remain independent, and shared application logic must not branch directly on hardware/platform identity.

# MiniFT8

MiniFT8-V3 is a substantial portable MiniShell application. Its MiniShell runtime name is `ft8`.

Application source lives here; the canonical application documentation lives under:

```text
docs/MiniFT8/
```

The application core depends on MiniShell services rather than Linux, NuttX, ESP-IDF, board APIs, or test mocks. Platform-specific providers stay below the MiniShell API.

The runtime application is intentionally **FT8-only**. Other protocols such as FT4, CW, RTTY, and JS8 are separate future MiniShell applications rather than modes inside `ft8`.

## Presentation profiles

P1 defines two MiniFT8 application presentations:

```text
DESKTOP   30 x 8 text frame, including a contextual footer
ADV       20 x 7 text frame, six main lines and no footer
```

They use the same controller, configuration, scheduler settings, `UiModel`, navigation, and MiniShell Display/Input adapter.

Presentation is launch/composition policy rather than backend identity or station configuration.

Linux:

```text
M$> ft8                    # DESKTOP default
M$> ft8 --profile desktop
M$> ft8 --profile adv
```

Current development policy is Linux backend first and ADV presentation for MiniFT8 UI work until a real ADV backend dependency must be exercised.

Cardputer ADV statically packages the same MiniFT8 source files and supplies `ADV` as the composition default:

```text
M$> ft8                    # ADV default on the ADV firmware
```

There is no runtime platform check inside MiniFT8 to select this. The small ADV static wrapper only supplies the application default during composition.

The ADV/DESKTOP presentation is not persisted in `/flash/ft8/station.txt`. The O-screen `Profile: Default` item is a separate station/operating-profile concept.

## P1/P2/V1 status

The cross-backend/profile checkpoint is complete.

```text
Linux + DESKTOP   PASS
Linux + ADV       PASS
ADV   + ADV       PASS
```

Validated behavior includes:

```text
app discovery and foreground lifecycle
shared UI actions/state transitions
ADV text 20x7 presentation
configuration persistence through /flash/ft8/station.txt
repeated ADV ft8 launch/exit cycles
stable ADV memory baseline
no direct platform dependencies under apps/ft8/
```

The canonical checkpoint record is:

```text
docs/MiniFT8/v1-validation.md
```

During P2, MiniShell also moved ADV foreground applications onto a dedicated 16 KiB application task instead of borrowing ESP-IDF's `app_main` stack. The portable `free` utility provides a useful ADV baseline before RX/DSP work:

```text
heap free       about 282 KiB
largest block   about 228 KiB
app allocations 0 after ft8 exits
```

The baseline remained effectively unchanged across repeated `ft8` launch/exit cycles.

## RX status

The cleaned pure RX domain now reaches `RxBatch` without MiniShell Audio, UI, AutoSeq, or platform dependencies:

```text
12 kHz / S16 / 2-channel
    -> RxFrontend
       6 kHz mono float
    -> RxSlotFramer
       exact slot/sample accounting
       960-sample engine blocks
    -> Ft8Engine
       monitor/waterfall
       candidate search
       likelihood/LDPC/CRC
       Ft8HashStore
       typed protocol codec
       exact-payload dedupe
    -> Ft8ProtocolSlot
    -> RxResultBuilder
    -> RxBatch
```

`app_controller` remains the sole application coordinator. The RX modules are passive/stateful owners underneath it; RX-5 deliberately does not introduce an `rx_pipeline` or second RX manager.

### RX-1 — clean FT8 engine — complete

RX-1A through RX-1G froze the V2 golden boundaries and cleaned monitor, decoder, callsign-hash, protocol-codec, and engine ownership behind one explicit `Ft8Engine` lifecycle.

Hard FT8 anchors remain:

```text
waterfall FNV-1a-64   18BE1E838FD9C6AF
payload                000000206016500A1988
canonical text         CQ W1XYZ FN42
```

### RX-2 — host decoder — implemented

RX-2 provides the Linux development utility:

```text
apps/ft8/tools/ft8_decode.c
```

It is not a MiniShell runtime application and has no presentation profile. It accepts one 6 kHz/mono/S16 PCM WAV decode window and prints every unique decoded FT8 message to stdout.

Example:

```text
./build/ft8_decode ft8_cq_w1xyz_fn42.wav
CQ W1XYZ FN42
```

The pinned RX-2 CI reference passes. Manual pc-1 validation remains pending but does not block later structural stages.

Canonical record:

```text
docs/MiniFT8/rx-2-host-decoder.md
```

### RX-3 — frontend — complete

```text
12 kHz S16 two-channel
    -> RxFrontend
       ordinary-audio channel policy
       S16 normalization/downmix
       retained 2:1 decimation phase
    -> 6 kHz mono float
```

The baseline preserves the pinned V2 conversion style: normalize L/R, average the channels, then use simple 2:1 decimation. No new filtering/resampling algorithm is mixed into structural cleanup.

Transport chunk boundaries are invisible because `RxFrontend` owns decimation phase across calls. The golden deliberately uses odd 257-frame chunks and still decodes `CQ W1XYZ FN42`.

Canonical records:

```text
docs/MiniFT8/rx-3-frontend.md
apps/ft8/src/rx_frontend/README.md
```

### RX-4 — slot framer — complete

`RxSlotFramer` owns exact 6 kHz sample-count timing after an initial ordinary timing reference establishes `slot_id + sample_offset`.

For one 15-second FT8 slot:

```text
90000 samples total
93 x 960 = 89280 samples delivered to Ft8Engine
720-sample slot-end remainder discarded
```

A first partial slot after stream start/discontinuity is discarded. Stream reset and ordinary next-window behavior remain distinct.

Reference:

```text
RX4 samples=90000 blocks=93 slot=12345 text="CQ W1XYZ FN42"
```

Canonical records:

```text
docs/MiniFT8/rx-4-slot-framer.md
apps/ft8/src/rx_slot_framer/README.md
```

### RX-5 — pure RX assembly — complete

RX-5 adds `RxResultBuilder` and proves the complete pure MiniFT8 chain from locked 12 kHz transport through factual application results:

```text
12 kHz S16 stereo
    -> RxFrontend
    -> RxSlotFramer
    -> Ft8Engine
    -> Ft8ProtocolSlot
    -> RxResultBuilder
    -> RxBatch
```

`RxResultBuilder` preserves exact payload/protocol/decoder facts and adds only factual application classifications such as `is_cq` and `is_to_me`. It owns no allocator, MiniShell service, UI policy, AutoSeq state, reply selection, or TX state.

The FREE_TEXT CQ exception remains explicit: protocol type stays `FREE_TEXT`, while only `CQ <nnn|AAAA> <valid-callsign> [grid]` may additionally be classified as logical CQ.

The RX-5 full-chain golden passes:

```text
RX5 slot=12345 blocks=93 messages=1 cq=1 to_me=0 text="CQ W1XYZ FN42"
rx5_pure_assembly_reference: PASS
```

Canonical records:

```text
docs/MiniFT8/rx-5-pure-assembly.md
apps/ft8/src/rx_result_builder/README.md
```

## Current RX development gate

```text
RX-2  IMPLEMENTED / pc-1 manual validation pending
RX-3  COMPLETE
RX-4  COMPLETE
RX-5  COMPLETE
RX-6  NEXT — MiniShell Audio + rx_audio_adapter integration on Linux
```

RX-6 will connect the existing MiniShell Audio API and Linux WAV provider without changing the pure RX domain:

```text
MiniShell Audio
    -> rx_audio_adapter
    -> RxFrontend
    -> RxSlotFramer
    -> Ft8Engine
    -> RxResultBuilder
    -> RxBatch
```

Changing the provider later from WAV to QMX must not require changes inside the pure RX modules.

Current application integration still includes the text UI, configuration, scheduler settings, and persistent `/flash/ft8/station.txt` through MiniShell Display, Input, and Filesystem services. MiniShell Audio V1 and the deterministic Linux WAV RX provider already exist; RX-6 is where they become the source for the cleaned receive path.

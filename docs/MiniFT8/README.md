# MiniFT8-V3

MiniFT8-V3 is the portable FT8 application hosted by MiniShell. Its MiniShell runtime application name is:

```text
ft8
```

Other digital protocols are separate future MiniShell applications rather than modes inside `ft8`.

## Boundary

```text
MiniFT8 application core
        |
        v
MiniShell public API
        |
        v
MiniShell backend/provider
        |
        v
Linux / future NuttX / ADV hardware / mocks
```

MiniFT8 application code must not depend directly on Linux, NuttX, ESP-IDF, board APIs, USB/UART/I2S, or test mocks.

## Current development policy

```text
backend       Linux
presentation  ADV when UI is involved
```

Linux remains the production/reference development target until a genuine embedded-backend dependency needs to be exercised.

## Current RX path

RX-7 completes the decode-RX milestone through the normal `ft8` application:

```text
MiniShell Audio
12 kHz / S16 / 2-channel
    -> rx_audio_adapter
    -> RxFrontend
       6 kHz mono float
    -> RxSlotFramer
       exact 960-sample Ft8Engine blocks
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
    -> app_controller
    -> UiModel
    -> ADV 20x7 presentation
```

`app_controller` remains MiniFT8's sole production application coordinator. RX-7 does not introduce an `rx_pipeline`, `rx_manager`, or second coordinator.

Current gate:

```text
RX-0        COMPLETE
RX-1A..1G  COMPLETE
RX-2        IMPLEMENTED / manual pc-1 test pending
RX-3        COMPLETE
RX-4        COMPLETE
RX-5        COMPLETE
RX-6        COMPLETE
RX-7        COMPLETE — real decoded RX UI, Linux reference + ADV build
```

RX-2's manual pc-1 validation does not block the structural sequence because its pinned Linux reference is green.

## RX ownership

```text
MiniShell
    owns Audio provider/device/backend resource

rx_audio_adapter
    owns MiniFT8's public RX Audio stream handle lifecycle

RxFrontend
    owns 12 kHz S16 two-channel -> 6 kHz mono-float adaptation

RxSlotFramer
    owns sample-count slot timing and one bounded 960-sample accumulator

Ft8Engine
    owns FT8 DSP/protocol/hash state

RxResultBuilder
    owns factual application projection into RxBatch

app_controller
    owns production application coordination/policy
```

## Locked transport / timing contracts

MiniShell Audio transport:

```text
12000 Hz
signed 16-bit PCM
2 ordered channels
```

MiniFT8 engine-native stream:

```text
6000 Hz
mono float
960 samples per Ft8Engine block
```

For one 15-second FT8 slot:

```text
90000 total 6 kHz samples
89280 samples in 93 complete 960-sample blocks
720 slot-end samples discarded
```

The first partial slot after stream start/discontinuity is also discarded. Time establishes only the initial slot reference; sample count owns progression afterward. `Ft8Engine` never reads a clock.

## Golden anchors

Pinned MiniFT8-V2 baseline:

```text
5bd3ef98f72388a850bebad04bd7300b90edb63c
```

Hard FT8 structural anchors:

```text
active-waterfall FNV-1a-64  18BE1E838FD9C6AF
unique CQ payload            000000206016500A1988
canonical CQ text            CQ W1XYZ FN42
```

RX-6 public-Audio proof:

```text
M$> RX6 frames=180000 slot=12345 blocks=93 messages=1 text="CQ W1XYZ FN42"
ft8_rx_probe: PASS
```

RX-7 production-application proof:

```text
M$> ft8 --profile adv --rx /flash/rx7.wav --rx-slot 12345
RX 20 HH:MM:SS 1/1 <0-E>
1 CQ W1XYZ FN42
```

The RX-7 reference launches the real `ft8` application through MiniShell, streams the pinned golden through the complete RX chain, and verifies the decoded message on the ADV RX screen. The ADV ESP32-S3 firmware build is also green.

## Presentation

MiniFT8 currently has two application presentation profiles:

```text
DESKTOP   30 x 8
ADV       20 x 7
```

Linux can launch either:

```text
M$> ft8 --profile desktop
M$> ft8 --profile adv
```

The ADV status line follows the locked 20-character UI definition and RX messages page six at a time with wraparound.

## ADV baseline

P1/P2/V1 are complete and real ADV hardware has validated application discovery, launch/exit, ADV 20x7 presentation, configuration persistence, and repeated `ft8` lifecycle behavior.

Pre-RX ADV baseline:

```text
heap free       ~282 KiB
largest block   ~228 KiB
```

RX-7 CI proves the integrated RX code cross-compiles for ESP32-S3. Live ADV Audio/provider behavior remains a later hardware integration concern.

## Canonical documentation

Current plan and development gate:

```text
rx.md
development.md
```

RX stage records:

```text
rx-golden.md
rx-1b-design.md
rx-1c-monitor.md
rx-1d-decoder.md
rx-1e-hash-store.md
rx-1f-message-codec.md
rx-1g-engine.md
rx-2-host-decoder.md
rx-3-frontend.md
rx-4-slot-framer.md
rx-5-pure-assembly.md
rx-6-minishell-audio.md
rx-7-decoded-ui.md
```

Other major documents:

```text
architecture.md
ui.md
v1-validation.md
```

## Current source shape

```text
apps/ft8/
├── main/
├── include/ft8/
└── src/
    ├── app_controller/
    ├── config_service/
    ├── presentation_profile/
    ├── qso_scheduler/
    ├── storage_service/
    ├── ui_shell/
    ├── ft8_engine/
    ├── rx_audio_adapter/
    ├── rx_frontend/
    ├── rx_slot_framer/
    └── rx_result_builder/
```

## After RX-7

The decode-RX milestone stops here. AutoSeq, TX, ADIF, live QMX integration, and the remaining detailed UIScreen work stay separate major blocks. The next major development block is intentionally not selected in this document.

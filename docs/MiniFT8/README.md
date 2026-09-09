# MiniFT8-V3

MiniFT8-V3 is the portable FT8 application hosted by MiniShell. The MiniShell runtime application name is:

```text
ft8
```

Other digital modes are separate future MiniShell applications rather than modes inside `ft8`.

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

Stay on Linux until a genuine ADV-backend dependency needs to be exercised.

## Current RX path

RX is now validated through the MiniShell public Audio boundary:

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
```

`app_controller` remains MiniFT8's sole production application coordinator. RX-6 did not introduce a second RX manager/pipeline abstraction.

Current gate:

```text
RX-0        COMPLETE
RX-1A..1G  COMPLETE
RX-2        IMPLEMENTED / manual pc-1 test pending
RX-3        COMPLETE
RX-4        COMPLETE
RX-5        COMPLETE
RX-6        COMPLETE
RX-7        NEXT — decoded RX UI, Linux + ADV presentation
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
    owns application coordination/policy
```

An application/module failure should remain local and must not destabilize MiniShell or unrelated applications.

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

The first partial slot after stream start/discontinuity is also discarded. Time establishes only the initial slot reference; sample count owns progression afterward. `rx_audio_adapter` owns Audio lifecycle, not UTC.

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

RX-6 full MiniShell proof:

```text
M$> RX6 frames=180000 slot=12345 blocks=93 messages=1 text="CQ W1XYZ FN42"
ft8_rx_probe: PASS
```

The Linux WAV provider can later be replaced by QMX or another MiniShell Audio provider without changing the pure RX modules.

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

Presentation is launch/composition policy, not backend identity and not persisted station configuration.

## ADV baseline

P1/P2/V1 are complete and real ADV hardware has validated application discovery, launch/exit, ADV 20x7 presentation, configuration persistence, and repeated `ft8` lifecycle behavior.

Pre-RX ADV baseline:

```text
heap free       ~282 KiB
largest block   ~228 KiB
```

Do not optimize RX RAM prematurely; measure the integrated workload first.

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
```

Other major documents:

```text
architecture.md
ui.md
v1-validation.md
```

Source-local ownership notes are kept in `README.md` files beside the corresponding modules.

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

## Next: RX-7

RX-7 moves the validated receive chain into the normal `ft8` application and renders real `RxBatch` results on the RX screen using the **Linux backend + ADV 20x7 presentation** first.

AutoSeq, TX, and ADIF remain separate major blocks and are not pulled into RX merely to demonstrate decoding.

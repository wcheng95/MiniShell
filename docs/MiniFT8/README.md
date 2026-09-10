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

`app_controller` remains MiniFT8's sole production application coordinator.

Current RX default:

```text
time_osr = 2
freq_osr = 2
```

The low-memory/V2-compatible reference remains `2x1`.

Real production fixture result using `tests/kfs16b12k.wav`:

```text
Linux  16 decoded messages
ADV    16 decoded messages
CQ      8 messages
```

## Current gate

```text
RX-0        COMPLETE
RX-1A..1G  COMPLETE
RX-2        IMPLEMENTED / manual pc-1 test pending
RX-3        COMPLETE
RX-4        COMPLETE
RX-5        COMPLETE
RX-6        COMPLETE
RX-7        COMPLETE

AS-0        COMPLETE — plan/reference freeze/boundary audit
AS-1        NEXT — station identity + factual SNR + RX-selection boundary
AS-2..AS-8 PLANNED — V2-equivalent compact AutoSeq port and closure
```

AutoSeq is now the selected next major block. The port changes boundary, ownership, and data representation first while preserving current MiniFT8-V2 AutoSeq behavior pinned at:

```text
wcheng95/Mini-FT8
491e757ae6b1e4cfd2b9a6ba10f48b35643849e0
```

See `as-plan.md` and `as-boundary-audit.md`.

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

## AutoSeq ownership

Planned V3 boundary:

```text
RxBatch / RxMessage
        |
        v
app_controller
        |
        v
     auto_seq
     /      \
QsoView    TxIntent / policy events
   |              |
   v              v
UiModel/T     app_controller
screen        future TX/logging
```

`auto_seq` will own the fixed QSO queue, state progression, retry/priority policy, active/inactive behavior, and V2-equivalent scheduling policy. It will not call MiniShell, DSP/hash code, UI code, file/logging I/O, Audio, Control, or platform APIs.

The current `qso_scheduler` is only a prototype settings holder and will be removed when `auto_seq` becomes the real owner.

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

Pinned original RX structural baseline:

```text
5bd3ef98f72388a850bebad04bd7300b90edb63c
```

Hard FT8 structural anchors:

```text
2x1 active-waterfall FNV-1a-64  18BE1E838FD9C6AF
unique CQ payload                 000000206016500A1988
canonical CQ text                 CQ W1XYZ FN42
```

Production 2x2 tuning is intentionally newer than that original structural boundary; both the production 2x2 and low-memory/reference 2x1 paths remain tested.

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

The ADV status line follows the locked 20-character UI definition and RX/TX entries page six at a time with wraparound.

## ADV memory reference

After the production 2x2 `kfs.wav` decode on ADV:

```text
heap free       111.3 KiB
largest block    53.0 KiB
app allocation  228.5 KiB
allocation count 2
RX               OFF
```

The corresponding shell baseline after the ADV RAM-squeeze work is about 342 KiB free. This makes compact fixed AutoSeq storage important, but AutoSeq should remain much smaller than the DSP workspace.

## Canonical documentation

Current plan and development gate:

```text
development.md
```

AutoSeq:

```text
as-plan.md
as-boundary-audit.md
```

RX:

```text
rx.md
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
rx-tuning.md
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
    ├── qso_scheduler/        # prototype; replaced by auto_seq in AS-2
    ├── storage_service/
    ├── ui_shell/
    ├── ft8_engine/
    ├── rx_audio_adapter/
    ├── rx_frontend/
    ├── rx_slot_framer/
    └── rx_result_builder/
```

## Next

Start AS-1 with three explicit boundary-completion tasks before QSO state-machine code:

```text
AS-1a  station callsign/grid ownership and RxResultBuilder injection
AS-1b  factual RX SNR carried into RxMessage
AS-1c  absolute RX-message selection AppAction resolved by app_controller
```

Then AS-2 can port the V2 AutoSeq core into a fixed-size C `auto_seq` module without violating existing ownership boundaries.

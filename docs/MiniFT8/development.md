# MiniFT8-V3 Development

MiniFT8 is a portable MiniShell application with independent RX Audio, TX Audio, and Control resources.

Current development rule:

```text
backend       Linux
presentation  ADV when UI is involved
```

Stay on the Linux backend until a real ADV backend dependency must be exercised. Pure host tools have no presentation profile.

MiniFT8-V2 remains the behavioral/golden reference. V2 structure is not copied wholesale.

## Current priority: RX-6 MiniShell Audio integration

The pure receive domain is now complete through `RxBatch`:

```text
12 kHz / S16 / 2-channel
        |
        v
RxFrontend
        |
        | 6 kHz mono float
        v
RxSlotFramer
        |
        | exact 960-sample Ft8Engine blocks
        v
Ft8Engine
        |
        v
Ft8ProtocolSlot
        |
        v
RxResultBuilder
        |
        v
RxBatch
```

RX-6 adds only the MiniShell-facing edge:

```text
MiniShell Audio
        |
        v
rx_audio_adapter
        |
        v
existing pure RX chain
```

`app_controller` remains the sole application coordinator. Do not introduce an `rx_pipeline`, `rx_manager`, or other second coordinator.

## Current stage status

```text
A0-A3       COMPLETE
P1          COMPLETE
P2          COMPLETE
V1          COMPLETE

RX-0        COMPLETE — V2 source/ownership review
RX-1A       COMPLETE — golden boundaries
RX-1B       COMPLETE — top-down RX ownership design
RX-1C       COMPLETE — monitor ownership/workspace
RX-1D       COMPLETE — candidate + LDPC + CRC
RX-1E       COMPLETE — explicit Ft8HashStore
RX-1F       COMPLETE — typed protocol codec
RX-1G       COMPLETE — pure Ft8Engine assembly
RX-2        IMPLEMENTED — host decoder; manual pc-1 validation pending
RX-3        COMPLETE — 12 kHz -> 6 kHz frontend
RX-4        COMPLETE — streaming slot framer
RX-5        COMPLETE — pure RX assembly -> RxBatch
RX-6        NEXT — MiniShell Audio + rx_audio_adapter on Linux
```

RX-2's pending manual pc-1 test does not block RX-6 because its pinned Linux CI reference is already green.

## Locked RX ownership

```text
MiniShell
    owns Audio device/provider/transport

rx_audio_adapter
    owns MiniFT8's MiniShell Audio stream handle and
    open/start/read/stop/close lifecycle

rx_frontend
    owns 12 kHz S16 two-channel -> 6 kHz mono-float
    adaptation state and decimation phase

rx_slot_framer
    owns slot identity, sample counting, and one
    bounded 960-sample accumulator

Ft8Engine
    owns FT8 monitor/waterfall/candidate/LDPC/CRC/
    callsign-hash/protocol state

rx_result_builder
    owns factual application projection into RxBatch

app_controller
    remains the sole coordinator
```

A module may use another module's service but must not reach through it and manipulate the underlying resource.

## Locked RX data contracts

MiniShell transport:

```text
sample rate  12000 Hz
format       signed 16-bit PCM
channels     2 ordered channels
```

MiniFT8 engine-native stream:

```text
sample rate  6000 Hz
format       mono float
block        960 samples
```

FT8 slot framing:

```text
15 s x 6000 = 90000 samples
93 x 960     = 89280 samples
remainder    =   720 samples
```

The 720-sample slot-end remainder is discarded; it is never carried into the next slot. A first partial slot after stream start/discontinuity is also discarded.

UTC/time supplies only the initial `slot_id + sample_offset` reference. Sample count owns progress after that.

## Locked structural-cleanup rules

1. Stream raw audio; retain the waterfall; retain full-slot PCM only by explicit exception.
2. Preserve the proven 6 kHz FT8-engine boundary during cleanup.
3. Preserve V2 FFT/OSR/candidate/LDPC/CRC/SNR behavior unless a structural blocker requires a narrowly documented change.
4. Exact payload bytes are authoritative protocol-message identity.
5. Protocol type is first-class; typed fields are authoritative and canonical text is derived convenience.
6. `Ft8HashStore` is explicit per-engine state, not global or MiniShell state.
7. Station identity may be factual decoder/application context but does not imply reply/TX policy.
8. FREE_TEXT may additionally be logical CQ only for `CQ <nnn|AAAA> <valid-callsign> [grid]`; protocol type remains FREE_TEXT.
9. Mathematical equivalence is not sufficient during DSP cleanup if the frozen golden changes.
10. App/module failure should remain local and must not destabilize MiniShell or unrelated applications.

## Golden anchors

Pinned MiniFT8-V2 baseline:

```text
5bd3ef98f72388a850bebad04bd7300b90edb63c
```

Hard FT8 anchors:

```text
active-waterfall FNV-1a-64  18BE1E838FD9C6AF
unique CQ payload            000000206016500A1988
canonical text               CQ W1XYZ FN42
```

RX-5 full pure-chain reference:

```text
RX5 slot=12345 blocks=93 messages=1 cq=1 to_me=0 text="CQ W1XYZ FN42"
rx5_pure_assembly_reference: PASS
```

## Canonical RX records

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
```

Source-local ownership notes live in the corresponding `README.md` files under `apps/ft8/src/`.

## RX-6 exit target

RX-6 should prove on Linux:

```text
MiniShell WAV Audio
12 kHz / S16 / 2-channel
        |
        v
rx_audio_adapter
        |
        v
RxFrontend
        |
        v
RxSlotFramer
        |
        v
Ft8Engine
        |
        v
RxResultBuilder
        |
        v
RxBatch
```

The key architecture test is provider substitution:

> Replacing the Linux WAV provider later with QMX or another MiniShell Audio provider must not require changes inside `rx_frontend`, `rx_slot_framer`, `ft8_engine`, or `rx_result_builder`.

RX-7 will be the decoded RX UI milestone. AutoSeq, TX, and ADIF remain separate major blocks and are not pulled into RX merely to demonstrate an end-to-end QSO.

# MiniFT8-V3 Development

MiniFT8 is a portable MiniShell application with independent RX Audio, TX Audio, and Control resources.

Current development rule:

```text
backend       Linux
presentation  ADV when UI is involved
```

Stay on Linux until a genuine ADV-backend dependency must be exercised. MiniFT8-V2 remains the behavioral/golden reference; V2 structure is not copied wholesale.

## Current priority: RX-7 decoded RX screen

RX-6 completed the MiniShell-facing receive edge. The validated receive path is now:

```text
MiniShell Audio
12 kHz / S16 / 2-channel
        |
        v
rx_audio_adapter
        |
        v
RxFrontend
        | 6 kHz mono float
        v
RxSlotFramer
        | exact 960-sample blocks
        v
Ft8Engine
        v
Ft8ProtocolSlot
        v
RxResultBuilder
        v
RxBatch
```

`app_controller` remains the sole production coordinator. RX-6 did not introduce an `rx_pipeline`, `rx_manager`, or second coordinator.

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
RX-6        COMPLETE — MiniShell Audio + Linux WAV integration
RX-7        NEXT — real decoded RX UI, Linux + ADV presentation
```

RX-2's pending manual pc-1 test does not block the structural sequence because its pinned Linux reference is green.

## Locked RX ownership

```text
MiniShell
    owns Audio provider/device/backend resource

rx_audio_adapter
    owns MiniFT8's public RX Audio stream handle and
    open/start/read/stop/close lifecycle

RxFrontend
    owns 12 kHz S16 two-channel -> 6 kHz mono-float
    adaptation state and decimation phase

RxSlotFramer
    owns slot identity, sample counting, and one
    bounded 960-sample accumulator

Ft8Engine
    owns FT8 monitor/waterfall/candidate/LDPC/CRC/
    callsign-hash/protocol state

RxResultBuilder
    owns factual application projection into RxBatch

app_controller
    remains the sole application coordinator/policy owner
```

A module may use another module's interface but must not reach through it and manipulate the underlying resource.

## Locked data/timing contracts

MiniShell transport:

```text
sample rate  12000 Hz
format       signed 16-bit PCM
channels     2 ordered channels
```

Engine-native stream:

```text
sample rate  6000 Hz
format       mono float
block        960 samples
```

FT8 slot:

```text
15 s x 6000 = 90000 samples
93 x 960     = 89280 samples
remainder    =   720 samples
```

The 720-sample slot-end remainder is discarded. A first partial slot after stream start/discontinuity is also discarded.

UTC/time supplies only the initial `slot_id + sample_offset`. Sample count owns progression after that. `rx_audio_adapter` does not own UTC.

## RX-6 proof

The dedicated RX-6 workflow launches a test app through the real MiniShell Linux runtime. That app opens a real 12 kHz/S16/stereo WAV using the public MiniShell Audio API and streams it through the unchanged RX-3/RX-4/RX-5 modules.

Result:

```text
M$> RX6 frames=180000 slot=12345 blocks=93 messages=1 text="CQ W1XYZ FN42"
ft8_rx_probe: PASS
```

The exact payload remains:

```text
000000206016500A1988
```

The key architecture proof is satisfied:

> A future QMX or other MiniShell Audio provider can replace the Linux WAV provider without changing `RxFrontend`, `RxSlotFramer`, `Ft8Engine`, or `RxResultBuilder`.

## Locked structural rules

1. Stream raw audio; retain the waterfall; retain full-slot PCM only by explicit exception.
2. Preserve the proven 6 kHz FT8-engine boundary during cleanup.
3. Preserve V2 FFT/OSR/candidate/LDPC/CRC/SNR behavior unless a separately measured change is intended.
4. Exact payload bytes are authoritative protocol-message identity.
5. Protocol type is first-class; typed fields are authoritative and canonical text is derived convenience.
6. `Ft8HashStore` is explicit per-engine state, not global or MiniShell state.
7. Station identity may be factual context but does not imply reply/TX policy.
8. FREE_TEXT may additionally be logical CQ only for `CQ <nnn|AAAA> <valid-callsign> [grid]`; protocol type remains FREE_TEXT.
9. App/module failure should remain local and must not destabilize MiniShell or unrelated applications.

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
rx-6-minishell-audio.md
```

Source-local ownership notes live in the corresponding `README.md` files under `apps/ft8/src/`.

## RX-7 exit target

RX-7 should consume real `RxBatch` results in the normal `ft8` application and render them on the RX screen using the **Linux backend + ADV 20x7 presentation** first.

RX-7 must not pull in AutoSeq, TX, or ADIF merely to demonstrate decoding. Those remain separate major blocks after the decode-RX milestone.

# MiniFT8-V3 RX Architecture and Development Plan

## 1. Milestone

The MiniFT8-V3 **decode RX** milestone is complete through RX-7.

The remaining major radio-domain blocks are intentionally separate:

```text
AutoSeq
TX
ADIF log
live radio/provider integration
```

MiniFT8-V2 remains the behavioral/golden reference, not a structure to copy wholesale. Structural cleanup stays separate from deliberate DSP/algorithm changes.

## 2. Canonical RX pipeline

```text
MiniShell Audio
12 kHz / S16 / 2-channel
        |
        v
[1] rx_audio_adapter
        |
        v
[2] RxFrontend
        | 6 kHz mono float
        v
[3] RxSlotFramer
        | exact 960-sample Ft8Engine blocks
        v
[4] Ft8Engine
        +--> monitor / waterfall
        +--> candidate finder
        +--> likelihood / LDPC / CRC
        +--> Ft8HashStore
        `--> typed protocol message codec
        |
        v
 Ft8ProtocolSlot
        |
        v
[5] RxResultBuilder
        |
        v
      RxBatch
        |
        v
 app_controller
        |
        v
      UiModel
        |
        v
 ADV 20x7 presentation
```

`app_controller` remains the only production application coordinator. There is no second RX manager/pipeline coordinator.

## 3. Ownership

| Block | Owns | Must not own |
| --- | --- | --- |
| `rx_audio_adapter` | MiniFT8's MiniShell Audio stream handle and open/start/read/stop/close lifecycle | provider/device/backend details, FT8 DSP, slot timing, channel meaning |
| `RxFrontend` | source/profile channel interpretation and 12 kHz S16/two-channel -> 6 kHz mono-float adaptation state | MiniShell provider behavior, slot timing, FT8 candidate policy |
| `RxSlotFramer` | slot identity, exact 6 kHz sample accounting, one bounded partial 960-sample accumulator | whole-slot PCM, FFT, protocol decoding |
| `Ft8Engine` | monitor/workspace, candidate array, decoder policy, persistent hash store, protocol decode, payload dedupe, current window identity | MiniShell, UI, wall-clock pacing, QSO policy, AutoSeq, TX |
| `RxResultBuilder` | factual application normalization/classification and caller-owned `RxBatch` construction | FFT/LDPC/CRC, MiniShell calls, reply/TX/UI policy |
| `app_controller` | production composition, initial timing reference, latest batch, application/UI coordination | provider internals, FT8 DSP internals |

MiniShell owns the physical/provider Audio resource underneath the public stream handle.

## 4. Factual classification versus QSO policy

RX may derive facts such as:

```text
is_cq
is_to_me
resolved callsign/grid fields
DXpedition relevance to local station
```

RX must not decide:

```text
should_reply
selected_station
next_tx_stage
TX armed
IgnoreList action
logging policy
```

Those remain above the RX domain.

## 5. Streaming and RAM rule

Normal RX is streaming. A complete FT8 slot of raw PCM is not a normal RAM requirement.

```text
bounded reusable transport/frontend buffers
    -> bounded 960-sample framer accumulator
    -> bounded FFT/workspace memory
    -> whole decode-window waterfall
```

At the MiniShell Audio boundary:

```text
12000 frames/s x 2 channels x 2 bytes = 48000 bytes/s
```

A 15-second raw stereo slot is about 720 kB, so whole-slot PCM storage would defeat the Cardputer-class memory goal.

Locked rule:

> Stream raw audio; retain the waterfall; retain raw PCM only by explicit exception.

## 6. Timing contract

For live reception:

```text
UTC/time establishes initial slot_id + sample_offset
sample count owns progress after that
```

`app_controller` obtains the initial MiniShell UTC reference. `Ft8Engine` never reads a clock. `RxSlotFramer` never calls MiniShell Time directly. `rx_audio_adapter` owns Audio lifecycle, not UTC.

FT8 engine rate and slot size:

```text
sample rate  6000 Hz
slot         15 s
slot samples 90000
block        960 samples
93 blocks    89280 samples
remainder      720 samples
```

The 720-sample slot-end remainder is discarded rather than carried into the next slot. The first partial slot after stream start or discontinuity is also discarded.

For deterministic file/replay tests, the caller may supply a known initial `slot_id + sample_offset`; real-time sleeping is unnecessary.

## 7. Audio semantics

MiniShell transports two ordered channels without assigning their meaning. MiniFT8 source/profile semantics decide how they are interpreted.

Current ordinary-audio baseline:

```text
12 kHz / S16 / two channels
    -> normalize L/R
    -> average L/R
    -> simple 2:1 decimation
    -> 6 kHz mono float
```

`RxFrontend` owns decimation phase across arbitrary Audio reads so provider/read chunk boundaries cannot alter the engine sample stream.

During structural cleanup the `Ft8Engine` input remains fixed at **6 kHz mono float**. Future filter, resampler, engine-rate, FFT, OSR, candidate-search, LDPC, or SNR changes are separate measured algorithm experiments.

## 8. Decoder/protocol rules

Pinned V2 FT8 baseline:

```text
sample rate             6000 Hz
f_min                     200 Hz
f_max                    2900 Hz
time_osr                     2
freq_osr                     1
candidate capacity           50
minimum sync score            5
max LDPC iterations          25
```

Candidate sync score is not SNR. Exact 10-byte payload bytes are authoritative protocol-message identity.

Supported cleaned RX families remain:

```text
STANDARD
NONSTD_CALL
FREE_TEXT
DXPEDITION
ARRL_FD
TELEMETRY
```

Protocol type is first-class. Typed protocol fields are authoritative; canonical rendered text is convenience.

`Ft8HashStore` is explicit per-engine state, not MiniShell state and not a process-global table.

## 9. Golden-reference policy

Pinned MiniFT8-V2 baseline:

```text
5bd3ef98f72388a850bebad04bd7300b90edb63c
```

Hard FT8 anchors:

```text
active-waterfall FNV-1a-64  18BE1E838FD9C6AF
unique CQ payload            000000206016500A1988
canonical CQ text            CQ W1XYZ FN42
```

Structural invariant:

```text
same input
    -> same reference-host waterfall
    -> same valid exact payloads
    -> same supported protocol semantics
```

Do not rewrite golden values merely because a refactor fails. Investigate the difference first.

## 10. Stage status

```text
RX-0   COMPLETE  architecture + V2 source review
RX-1A  COMPLETE  golden boundary freeze
RX-1B  COMPLETE  module/interface/ownership design
RX-1C  COMPLETE  Ft8Monitor explicit workspace/lifecycle
RX-1D  COMPLETE  candidate + likelihood/LDPC/CRC
RX-1E  COMPLETE  explicit Ft8HashStore
RX-1F  COMPLETE  typed protocol codec + Ft8ProtocolSlot
RX-1G  COMPLETE  pure Ft8Engine owner/lifecycle
RX-2   IMPLEMENTED  Linux 6 kHz host decoder; pc-1 manual test pending
RX-3   COMPLETE  RxFrontend 12 kHz -> 6 kHz
RX-4   COMPLETE  RxSlotFramer sample-count slot framing
RX-5   COMPLETE  pure RX assembly -> RxBatch
RX-6   COMPLETE  MiniShell Audio + Linux WAV integration
RX-7   COMPLETE  production decoded RX UI + ADV cross-build
```

Canonical records:

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

## 11. RX-6 proof

RX-6 proved the actual MiniShell public Audio boundary:

```text
M$> RX6 frames=180000 slot=12345 blocks=93 messages=1 text="CQ W1XYZ FN42"
ft8_rx_probe: PASS
```

A future QMX or other MiniShell Audio provider must plug in below this boundary without requiring changes to the pure RX modules.

## 12. RX-7 proof

RX-7 moves the same receive composition into the normal production `ft8` application. The application cooperatively steps bounded Audio reads, builds the latest `RxBatch`, projects the canonical decoded text into `UiModel`, and renders it with the ADV presentation.

Deterministic production replay:

```text
M$> ft8 --profile adv --rx /flash/rx7.wav --rx-slot 12345
RX 20 HH:MM:SS 1/1 <0-E>
1 CQ W1XYZ FN42
```

The RX-7 workflow verifies the real application path against the pinned V2 golden. The same RX-7 head also passes the Linux suite, RX reference workflows, and ESP-IDF v5.5.1 ESP32-S3 ADV firmware build.

RX pages expose the current `RxBatch` six lines at a time and wrap with Up/Down. The locked 20-character top line is implemented for ADV presentation.

RX-7 does not add AutoSeq, TX, ADIF, a QMX provider, or an ADV live Audio provider.

## 13. Milestone boundary

Stop the decode-RX milestone here. AutoSeq, TX, ADIF, and live radio/provider work remain separate major blocks. No next major block is selected by this RX document.

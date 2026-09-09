# MiniFT8-V3 RX Architecture and Development Plan

## 1. Milestone

The current MiniFT8-V3 milestone is **decode RX**.

The remaining major radio-domain blocks are intentionally separate:

```text
RX
AutoSeq
TX
ADIF log
```

RX is developed and understood first. AutoSeq, TX, and ADIF remain outside this milestone.

MiniFT8-V2 is the behavioral/golden reference, not a structure to copy wholesale. Structural cleanup remains separate from deliberate DSP/algorithm changes.

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
        |
        | 6 kHz mono float
        v
[3] RxSlotFramer
        |
        | exact 960-sample Ft8Engine blocks
        v
[4] Ft8Engine
        |
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
        +--> UI
        `--> future AutoSeq
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

MiniShell owns the physical/provider Audio resource underneath the public stream handle. A module may consume another module's interface but must not reach through it and manipulate the underlying resource.

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
UI priority/order
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

`Ft8Engine` never reads a clock. `RxSlotFramer` never calls MiniShell Time directly. `rx_audio_adapter` owns Audio lifecycle, not UTC.

FT8 engine rate and slot size:

```text
sample rate  6000 Hz
slot         15 s
slot samples 90000
block        960 samples
```

Therefore:

```text
93 x 960 = 89280 complete engine samples
remainder = 720 samples
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

During structural cleanup the `Ft8Engine` input remains fixed at **6 kHz mono float**. Any future filter, resampler, engine-rate, FFT, OSR, candidate-search, LDPC, or SNR change is a separate measured algorithm experiment.

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

Protocol type is first-class. Typed protocol fields are authoritative; canonical rendered text is convenience.

Supported cleaned RX families remain:

```text
STANDARD
NONSTD_CALL
FREE_TEXT
DXPEDITION
ARRL_FD
TELEMETRY
```

### FREE_TEXT CQ exception

Protocol type remains `FREE_TEXT`, but `RxResultBuilder` may additionally set `is_cq=true` only when canonical text matches:

```text
CQ <nnn|AAAA> <valid-callsign> [valid-grid]
```

This is factual classification and does not imply a reply.

### Callsign hash ownership

```text
Ft8Engine
    `-- Ft8HashStore
```

The hash store is explicit per-engine state, not MiniShell state and not a process-global table.

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
RX-7   NEXT      real decoded RX screen
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
```

## 11. RX-6 proof

RX-6 is the first proof through the actual MiniShell public Audio boundary:

```text
Linux WAV provider
    -> MiniShell Audio
    -> rx_audio_adapter
    -> RxFrontend
    -> RxSlotFramer
    -> Ft8Engine
    -> Ft8ProtocolSlot
    -> RxResultBuilder
    -> RxBatch
```

The Linux fixture is a real 15-second 12 kHz/S16/stereo WAV built from the pinned V2 CQ golden. The MiniShell test application reads it through the public Audio API in bounded 257-frame chunks.

Result:

```text
M$> RX6 frames=180000 slot=12345 blocks=93 messages=1 text="CQ W1XYZ FN42"
ft8_rx_probe: PASS
```

No changes were required inside `RxFrontend`, `RxSlotFramer`, `Ft8Engine`, or `RxResultBuilder` to add MiniShell Audio.

Provider-substitution rule:

> A future QMX or other MiniShell Audio provider must plug in below this boundary without requiring changes to the pure RX modules.

## 12. RX-7 — NEXT: decoded RX screen

RX-7 moves the validated receive composition into the normal `ft8` application and consumes real `RxBatch` results on the RX screen.

Development target remains:

```text
backend       Linux
presentation  ADV 20x7
```

RX-7 should establish the production timing reference at the coordinator edge and preserve the module ownership above. It must not pull in AutoSeq, TX, or ADIF.

Stop the decode-RX milestone after the real decoded RX UI is validated. AutoSeq, TX, and ADIF remain separate major blocks.

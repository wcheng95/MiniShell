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

`app_controller` remains the only application coordinator. RX-5 deliberately does not introduce an `rx_pipeline`, `rx_manager`, or other second coordinator.

## 3. Ownership

| Block | Owns | Must not own |
| --- | --- | --- |
| `rx_audio_adapter` | MiniFT8's MiniShell Audio stream handle and open/start/read/stop/close lifecycle | provider/device details, FT8 DSP, slot timing, channel meaning |
| `RxFrontend` | source/profile channel interpretation and 12 kHz S16/two-channel -> 6 kHz mono-float adaptation state | MiniShell provider behavior, slot timing, FT8 candidate policy |
| `RxSlotFramer` | slot identity, exact 6 kHz sample accounting, one bounded partial 960-sample accumulator | whole-slot PCM, FFT, protocol decoding |
| `Ft8Engine` | monitor/workspace, candidate array, decoder policy, persistent hash store, protocol decode, payload dedupe, current window identity | MiniShell, UI, wall-clock pacing, QSO policy, AutoSeq, TX |
| `RxResultBuilder` | factual application normalization/classification and caller-owned `RxBatch` construction | FFT/LDPC/CRC, MiniShell calls, reply/TX/UI policy |

A module may consume another module's interface but must not reach through it and manipulate the underlying resource.

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

Optional research paths may retain raw audio on hosts, but normal decode must not depend on that feature.

## 6. Timing contract

For live reception:

```text
UTC/time establishes initial slot_id + sample_offset
sample count owns progress after that
```

`Ft8Engine` never reads a clock. `RxSlotFramer` never calls MiniShell Time directly.

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

The 720-sample slot-end remainder is discarded rather than carried into the next slot.

The first partial slot after stream start or discontinuity is also discarded. Normal next-window transition and stream discontinuity remain separate operations.

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

`RxFrontend` owns decimation phase across arbitrary transport reads so transport chunk boundaries cannot alter the engine sample stream.

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

Candidate sync score is not SNR.

Exact 10-byte payload bytes are authoritative protocol-message identity. CRC or quick hashes may be diagnostics/accelerators but are not collision-free identity.

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

Recognized-but-unimplemented families remain explicit unsupported results rather than being silently approximated.

### FREE_TEXT CQ exception

Protocol type remains `FREE_TEXT`, but `RxResultBuilder` may additionally set `is_cq=true` only when canonical text matches:

```text
CQ <nnn|AAAA> <valid-callsign> [valid-grid]
```

Examples:

```text
CQ POTA K7XYZ     -> logical CQ
CQ 123 K7XYZ DM43 -> logical CQ
CQ HELLO WORLD    -> not logical CQ
```

This remains factual classification and does not imply a reply.

### Callsign hash ownership

```text
Ft8Engine
    `-- Ft8HashStore
```

The pinned V2 behavior remains:

```text
capacity          128 entries
entry             16 bytes
stored hash       full 22-bit hash
lookup widths     22 / 12 / 10 bits
bucket            (top10 * 23) % 128
age               uint8 saturating
save/lookup       refresh age to zero
trim holes        lookup scans through holes
full-table policy 128 -> trim to 78 -> insert -> 79
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

RX-1C demonstrated why: a mathematically equivalent reassociation of Hann-window float multiplication changed the compact waterfall fingerprint. Restoring V2's exact operation grouping restored the golden.

## 10. Completed stages

Detailed records are kept separately so this file can remain the concise canonical plan.

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
```

## 11. RX-5 proof

RX-5 is the first full pure MiniFT8 receive-domain proof:

```text
12 kHz S16 stereo
    -> RxFrontend
    -> RxSlotFramer
    -> Ft8Engine
    -> Ft8ProtocolSlot
    -> RxResultBuilder
    -> RxBatch
```

The reference deliberately uses odd 257-frame transport chunks so frontend decimation pairs repeatedly cross read boundaries. The remainder of the 15-second slot is zero-filled.

Result:

```text
rx_result_builder_rx5_test: PASS
RX5 slot=12345 blocks=93 messages=1 cq=1 to_me=0 text="CQ W1XYZ FN42"
rx5_pure_assembly_reference: PASS
```

No MiniShell service is involved in this proof.

## 12. RX-6 — NEXT: MiniShell Audio integration

RX-6 adds the missing MiniShell-facing owner:

```text
MiniShell Audio
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

`rx_audio_adapter` belongs to MiniFT8. It owns MiniFT8's use of a MiniShell Audio stream handle:

```text
open
start
read
stop
close
```

MiniShell still owns the actual provider/device/transport resource.

Initial RX-6 target is the Linux backend and deterministic MiniShell WAV Audio provider. The critical architecture proof is:

> Replacing the WAV provider later with QMX or another MiniShell Audio provider must not require changes inside `RxFrontend`, `RxSlotFramer`, `Ft8Engine`, or `RxResultBuilder`.

## 13. Later RX stage

```text
RX-7  real decoded RX UI using the ADV presentation on Linux first
```

Stop the decode-RX milestone there. AutoSeq, TX, and ADIF remain separate major blocks.

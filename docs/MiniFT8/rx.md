# MiniFT8-V3 RX Architecture and Development Plan

## 1. Milestone

The next MiniFT8-V3 milestone is **decode RX**.

MiniFT8 is intentionally being reduced to four major radio-domain blocks:

```text
RX
AutoSeq
TX
ADIF log
```

RX is developed and understood first. AutoSeq, TX, and ADIF remain outside this milestone.

MiniFT8-V2 is the behavioral/golden reference, not a structure to copy wholesale. V2 code may be kept, cleaned, rewritten, or dropped depending on ownership and interface quality. Structural cleanup must be separated from deliberate DSP/algorithm changes so behavioral differences remain explainable.

## 2. RX pipeline

The logical RX pipeline is:

```text
MiniShell Audio
12 kHz / S16 / 2-channel
        |
        v
[1] rx_audio_adapter
        |
        v
[2] rx_frontend
        |
        v
[3] rx_slot_framer
        |
        v
[4] ft8_monitor
        |
        v
[5] candidate finder
        |
        v
[6] candidate decoder
        |
        v
[7] message/result builder
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

These are **logical ownership boundaries**, not a requirement for seven source files. Small adjacent mechanisms may share a module when doing so remains easy to explain and test.

## 3. Block ownership

| Block | Owns | Must not own |
| --- | --- | --- |
| `rx_audio_adapter` | MiniShell Audio reads and translation into MiniFT8-owned sample blocks | FT8 DSP, WAV/UAC/platform details, channel meaning |
| `rx_frontend` | MiniFT8 source/profile channel interpretation and ordinary-audio/IQ preprocessing | MiniShell provider behavior, slot timing, FT8 candidate policy |
| `rx_slot_framer` | slot identity and exact sample accounting inside a slot | whole-slot raw PCM ownership, FFT, decoding |
| `ft8_monitor` | streaming sample-to-waterfall/FFT analysis and its explicit workspace/state | UI, wall-clock pacing, AutoSeq, platform APIs |
| candidate finder | Costas/sync search over a completed waterfall | LDPC, UI, AutoSeq |
| candidate decoder | likelihood extraction, LDPC, CRC, decoded payload/status | UI, QSO policy |
| message/result builder | message unpacking, duplicate suppression, measurement metadata, `RxBatch` construction | reply policy, TX decisions, UI sorting policy |

`app_controller` remains the domain coordinator. RX does not directly call AutoSeq, TX, ADIF, or UI modules.

## 4. Streaming and RAM rule

Raw audio is a streaming resource.

Normal RX must **not** require a whole FT8 slot of PCM to be stored in RAM. The normal memory classes are:

```text
bounded audio streaming buffers
    reusable; independent of slot duration

bounded FFT working memory
    reusable; explicit monitor ownership

whole-slot waterfall
    retained until candidate search/decode completes
```

At the current MiniShell Audio boundary:

```text
12000 frames/s x 2 channels x 2 bytes = 48000 bytes/s
```

A 15-second whole-slot raw buffer would be about 720 kB, so it must not become an architectural requirement for Cardputer-class targets.

A test program may load an entire WAV file into host RAM for convenience, but production RX interfaces remain streaming from the start.

### Optional raw-audio research path

Later research algorithms may explicitly retain raw slot audio, including double buffering:

```text
                    +--> normal monitor/waterfall/decode
streaming samples --+
                    `--> optional raw-slot capture
                              |
                              +--> alternate algorithm A
                              `--> alternate algorithm B
```

This optional path is valuable because algorithms can consume the **same captured sample stream**, making comparisons meaningful. More capable hosts may enable it; embedded targets may leave it disabled. Normal decoding must not depend on it.

Locked RX memory rule:

> Stream raw audio; retain the waterfall; retain raw PCM only by explicit exception.

## 5. Timing rule

For live reception:

```text
UTC          establishes slot identity/boundary
sample count measures progress inside the slot
```

For deterministic file/replay tests, samples are the clock. No real-time sleeping is required.

`rx_slot_framer` therefore tracks slot/sample accounting but does not own a whole-slot PCM buffer.

## 6. Audio/channel rule

MiniShell transports ordered channel 0/channel 1 samples without assigning domain meaning.

MiniFT8 source/profile semantics decide interpretation:

```text
ordinary audio
    channel 0/1 -> select/downmix -> FT8 frontend

I/Q
    channel 0 = I
    channel 1 = Q
    -> future I/Q processing
```

The initial reference fixture `tests/kfs16b12k.wav` already matches the MiniShell transport format: 12 kHz, S16, two channels.

The FT8 engine's internal sample rate is **not yet locked** to V2's 6 kHz choice. We will preserve V2 behavior first, then compare a clean 12 kHz path versus a justified 12->6 kHz frontend conversion as a separate algorithm decision.

## 7. V2 source classification

Before code is moved, classify V2 sources as:

```text
KEEP      algorithm/interface already sufficiently clean
CLEAN     useful behavior but ownership/interface must be corrected
REWRITE   concept is useful but implementation is unsuitable for V3
DROP      responsibility moved to MiniShell or is obsolete
```

Initial map:

| MiniFT8-V2 source | V3 role | Initial decision |
| --- | --- | --- |
| `main/audio_source.cpp` | old physical RX-source selector | DROP as V3 transport architecture; reference behavior only |
| `main/stream_uac.cpp` | UAC/device transport | DROP from MiniFT8; belongs below MiniShell Audio |
| `main/stream_mic.cpp` | microphone/device transport | DROP from MiniFT8; belongs below MiniShell Audio |
| `main/resample.cpp` | channel conversion/resampling ideas | REWRITE after measured review |
| `main/ft8_audio_pipeline.cpp` | mixed RX pipeline | REWRITE/SPLIT; behavior reference only |
| `components/ft8_lib/common/monitor.[ch]` | FFT/waterfall engine | CLEAN substantially |
| `components/ft8_lib/ft8/decode.[ch]` | candidate search + candidate decode | KEEP algorithm first; clean interfaces/ownership only as needed |
| `components/ft8_lib/ft8/message.[ch]` | payload/message encode/decode + hashes | KEEP algorithm first; review state/interface boundaries |
| LDPC/CRC/constants dependencies | pure FT8 math/protocol core | KEEP unless review finds hidden state/platform coupling |
| `tests/tx_e2e/decode_helper.cpp` | simplest host decoder reference | KEEP as golden/reference concept; do not copy test WAV I/O into engine |

The classifications may change after source review.

## 8. Golden-reference rule

MiniFT8-V2 defines initial expected behavior.

Primary golden invariant:

```text
same input
    -> same valid decoded FT8 messages
```

Useful diagnostics to record include:

```text
candidate count
candidate score/order
frequency/time offset
decode status / LDPC / CRC information
payload/hash identity
message text
```

Candidate ordering, score, and reported SNR are diagnostic at first, not necessarily hard equality requirements. A later deliberate algorithm change may legitimately alter them while preserving or improving valid decoding.

Structural refactors must be isolated from algorithm changes:

```text
A  ownership/global cleanup       -> same decoder behavior
B  interface/module cleanup       -> same decoder behavior
C  resampling/FFT change          -> deliberate comparison
D  time_osr/freq_osr experiments  -> deliberate comparison
```

## 9. RX-0 through RX-7

### RX-0 — Golden reference and code map

- freeze this pipeline and ownership model;
- inventory the V2 RX/ft8_lib source subset;
- classify each file KEEP/CLEAN/REWRITE/DROP;
- establish golden WAV/reference cases and expected decoded messages;
- review the simple host decoder path before importing code.

### RX-1 — Study and clean FT8 decode core

Recommended reading order:

```text
constants
   -> monitor / FFT
   -> waterfall representation
   -> Costas sync / candidate search
   -> likelihood extraction
   -> LDPC
   -> CRC
   -> message unpack / callsign hash
```

Change structure before mathematics.

### RX-2 — Pure host FT8 decoder

Build a MiniShell-independent test path:

```text
known engine-native PCM
    -> cleaned ft8 core
    -> RxMessage[]
```

No UI, MiniShell, AutoSeq, or TX.

### RX-3 — MiniFT8 RX frontend

Build/test:

```text
12 kHz / S16 / 2-channel
    -> source/profile interpretation
    -> ordinary-audio select/downmix or future IQ path
    -> engine-native streaming samples
```

This is where the internal 12 kHz versus 6 kHz decision is measured and made.

### RX-4 — Streaming slot framing

Verify:

- exact slot/sample accounting;
- correct boundary behavior;
- partial first slot handling;
- no lost or duplicated samples;
- correct monitor reset/finalize behavior;
- memory does not grow with slot duration.

### RX-5 — Pure RX block

Assemble and test:

```text
bounded PCM blocks
    -> rx_frontend
    -> rx_slot_framer
    -> ft8_monitor
    -> candidate/decode/message pipeline
    -> RxBatch
```

No MiniShell requirement in the principal golden test.

### RX-6 — MiniShell WAV Audio integration

```text
tests/kfs16b12k.wav
    -> MiniShell WAV Audio provider
    -> rx_audio_adapter
    -> RX
    -> RxBatch
```

Changing the provider later from WAV to QMX must not change the RX domain pipeline.

### RX-7 — Real decoded RX UI

```text
RxBatch
    -> app_controller
    -> UiModel
    -> ui_shell
```

Replace prototype RX lines with real decoded messages, then stop the milestone. Do not add AutoSeq, TX, or ADIF as part of RX completion.

## 10. RX output contract direction

The exact C types are deferred until source review, but the domain result should conceptually provide:

```text
RxBatch
    slot identity
    message count
    messages[]

RxMessage
    decoded/structured message
    display text
    message type
    audio-frequency offset
    time offset
    signal/SNR metadata
    payload/hash identity
```

RX reports what was decoded. It must not decide whether a message is addressed to this station, whether to reply, which TX stage comes next, or which CQ should be preferred. Those policies belong above RX.

## 11. RX-0B: decoder-contract extraction from V2

`tests/tx_e2e/decode_helper.cpp` is the preferred first V2 reading because it exposes the decoder essence without firmware scheduling/UI/device code.

Its important chain is:

```text
PCM samples
    -> monitor_init/reset
    -> monitor_process(block)
    -> ftx_find_candidates(waterfall)
    -> ftx_decode_candidate(waterfall, candidate)
    -> ftx_message_decode(payload)
    -> decoded result
```

V3 should preserve this conceptual separation but remove test-only/file-specific concerns and hidden ownership.

The decoder contract we are working toward is:

```text
streaming engine-native samples
        |
        v
explicit Ft8Monitor instance
        |
        | finalized whole-slot waterfall
        v
candidate search
        |
        v
candidate decode
        |
        v
message decode
        |
        v
RxMessage[] / RxBatch
```

Important contract properties:

1. No WAV/file APIs inside the FT8 engine.
2. No MiniShell, Linux, ESP-IDF, FreeRTOS, UI, or AutoSeq dependency.
3. Monitor/workspace ownership is explicit per instance; no hidden singleton buffers.
4. Samples may arrive incrementally; whole-slot PCM is not required.
5. Candidate search operates on the finalized waterfall, not on transport state.
6. Candidate decoding returns protocol/payload/status data before application policy is applied.
7. Callsign-hash state is explicit decoder/application state, not an unexplained process-global test map.
8. One decode pass may return multiple unique messages; V3 must not inherit the test helper's "first successful decode only" limitation.
9. Resource requirements must be inspectable so Cardputer-class RAM remains a first-class design constraint.
10. Decoder cleanup and alternate algorithms must be testable against the same captured input/result contract.

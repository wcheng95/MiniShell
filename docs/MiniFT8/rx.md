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

The seven logical RX/DSP blocks are:

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
[7] protocol message codec
        |
        v
 Ft8ProtocolSlot
        |
        v
 rx_result_builder
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

Blocks 4-7 are naturally internal pieces of `ft8_engine`. `rx_result_builder` is deliberately outside the protocol engine because it performs station-aware factual normalization/classification.

These are **logical ownership boundaries**, not a requirement for seven source files. Small adjacent mechanisms may share a module when doing so remains easy to explain and test.

## 3. Block ownership

| Block | Owns | Must not own |
| --- | --- | --- |
| `rx_audio_adapter` | MiniShell Audio reads and translation into MiniFT8-owned sample blocks | FT8 DSP, WAV/UAC/platform details, channel meaning |
| `rx_frontend` | MiniFT8 source/profile channel interpretation and ordinary-audio/IQ preprocessing | MiniShell provider behavior, slot timing, FT8 candidate policy |
| `rx_slot_framer` | slot identity and exact sample accounting inside a slot | whole-slot raw PCM ownership, FFT, decoding |
| `ft8_monitor` | streaming sample-to-waterfall/FFT analysis and its explicit workspace/state | UI, wall-clock pacing, AutoSeq, platform APIs |
| candidate finder | Costas/sync search over a completed waterfall | LDPC, UI, AutoSeq |
| candidate decoder | likelihood extraction, LDPC, CRC, validated payload/status | message rendering, UI, QSO policy |
| protocol message codec | payload type classification, structured unpacking, callsign-hash resolution, canonical protocol text | mycall application semantics, reply policy, UI sorting, AutoSeq |
| `rx_result_builder` | station-aware logical normalization, typed CQ/to-me classification, free-text CQ exception, DXpedition relevance transformation, optional logical dedupe, `RxBatch` construction | FFT, LDPC, CRC, MiniShell calls, reply/TX policy |

`app_controller` remains the domain coordinator. RX does not directly call AutoSeq, TX, ADIF, or UI modules.

A useful distinction is:

```text
factual classification:
    is this message a CQ?
    is this message addressed to my station?
        -> RX result builder may answer

QSO policy:
    should I reply?
    which station should I work?
    which TX stage comes next?
        -> AutoSeq/application policy only
```

## 4. Streaming and RAM rule

Raw audio is a streaming resource.

Normal RX must **not** require a whole FT8 slot of PCM to be stored in RAM. The normal memory classes are:

```text
bounded audio streaming buffers
    reusable; independent of slot duration

bounded FFT working memory
    reusable; explicit monitor ownership

whole decode-window waterfall
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

RX-0 final map:

| MiniFT8-V2 source | V3 role | Decision |
| --- | --- | --- |
| `main/audio_source.cpp` | old physical RX-source selector | DROP as V3 transport architecture; reference behavior only |
| `main/stream_uac.cpp` | UAC/device transport | DROP from MiniFT8; belongs below MiniShell Audio |
| `main/stream_mic.cpp` | microphone/device transport | DROP from MiniFT8; belongs below MiniShell Audio |
| `main/resample.cpp` | channel conversion/resampling ideas | REWRITE after measured review |
| `main/ft8_audio_pipeline.cpp` | mixed RX pipeline | REWRITE/SPLIT; behavior reference only |
| `components/ft8_lib/common/monitor.[ch]` | FFT/waterfall engine | KEEP mathematics; CLEAN memory ownership/lifecycle substantially |
| `components/ft8_lib/ft8/decode.[ch]` | candidate search + likelihood/LDPC/CRC | KEEP algorithms; CLEAN policy/status/interface details |
| `components/ft8_lib/ft8/message.[ch]` | payload type/unpack + shared encode/hash mechanics | KEEP protocol mechanics; CLEAN typed representation/hash ownership substantially |
| LDPC/CRC/constants dependencies | pure FT8 math/protocol core | KEEP unless implementation work reveals a concrete issue |
| `tests/tx_e2e/decode_helper.cpp` | simplest host decoder reference | KEEP as golden/reference concept; do not copy test WAV I/O into engine |

TX-specific message parsing/packing cleanup is deferred to the TX milestone.

## 8. Golden-reference rule

MiniFT8-V2 defines initial expected behavior.

Primary golden invariant:

```text
same input
    -> same valid decoded FT8 messages/payloads/types
```

Useful diagnostics to record include:

```text
candidate count
candidate score/order
frequency/time offset
decode status / LDPC / CRC information
payload identity
protocol message type
message text
V2 SNR
```

Structural refactors must be isolated from algorithm changes:

```text
A  ownership/global cleanup       -> same decoder behavior
B  interface/module cleanup       -> same decoder behavior
C  targeted protocol bug fix      -> independent test/vector
D  resampling/FFT change          -> deliberate comparison
E  time_osr/freq_osr/deep search  -> deliberate comparison
```

The V2 SNR estimator is the initial baseline method. Candidate score and SNR remain separate. SNR improvement is deferred to a measured algorithm change.

Known type/codec gaps such as type 0.6 classification and currently unsupported known message types must be tested explicitly; they are not silently mixed into structural refactoring.

## 9. RX-0 through RX-7

### RX-0 — Golden reference and code map — complete

RX-0 established:

- seven logical RX/DSP boundaries;
- station-aware `rx_result_builder` boundary;
- streaming/RAM rule;
- V2 source KEEP/CLEAN/REWRITE/DROP classification;
- golden behavior policy;
- monitor ownership/reset semantics;
- candidate/decode policy boundaries;
- typed protocol-message/hash-store direction.

Canonical reviews are:

```text
rx-decoder-contract.md
rx-v2-production-review.md
rx-monitor-review.md
rx-decode-review.md
rx-message-review.md
```

### RX-1 — Clean FT8 decode core

Implement structure before mathematics.

Initial work order:

```text
1. establish/copy golden protocol + monitor tests
2. define MiniFT8-owned internal decoder/result types
3. clean monitor ownership/workspace with byte-identical waterfall target
4. bring candidate/LDPC/CRC core across with V2 policy profile
5. build explicit Ft8HashStore
6. build typed protocol message decoder
7. run golden tests after every structural step
```

No resampling, OSR, SNR, deep-search, or other algorithm improvement is mixed into these steps.

### RX-2 — Pure host FT8 decoder

Build a MiniShell-independent test path:

```text
known engine-native PCM
    -> cleaned ft8 core
    -> typed protocol messages
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
- correct monitor new-window/stream-discontinuity behavior;
- memory does not grow with slot duration.

### RX-5 — Pure RX block

Assemble and test:

```text
bounded PCM blocks
    -> rx_frontend
    -> rx_slot_framer
    -> ft8_engine
    -> Ft8ProtocolSlot
    -> rx_result_builder
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

## 10. Protocol and RX output direction

Exact C syntax is defined during RX-1, but the two-level result is now conceptually clear.

Protocol engine output:

```text
Ft8ProtocolSlot
    protocol
    message count
    messages[]

Ft8ProtocolMessage
    exact payload identity
    protocol message type
    parse status
    typed message-specific fields
    canonical rendered text
    candidate score
    frequency/time metadata
    V2-baseline SNR metadata
    optional LDPC/CRC diagnostics
```

The protocol type is always first-class. Known protocol type and structured-unpack support are different facts.

Application RX output:

```text
RxBatch
    slot identity
    message count
    RxMessage[]
```

`rx_result_builder` may add factual station-aware properties such as:

```text
is_cq
is_to_me
DXpedition logical relevance
```

and the explicit free-text CQ exception:

```text
protocol type = FREE_TEXT
text = CQ <nnn|AAAA> <valid-callsign> [valid-grid]
    -> logical is_cq = true
```

The protocol type remains `FREE_TEXT`.

RX still must not decide:

```text
whether to reply
which station to work
which TX stage comes next
whether to arm TX
```

Those are AutoSeq/application policies.

## 11. Decoder/search context

Normal FT8 decode semantics remain protocol-level. Future deep-search algorithms may accept explicit station-aware search/prior hints, including the local callsign, if that improves recovery of reply-to-me messages.

Conceptually:

```text
Ft8DecodeContext
    optional local_callsign
    deep_search_enabled
    future explicit algorithmic hints
```

This context may influence candidate generation/ranking or prior-assisted likelihood/LDPC decoding. It must not carry AutoSeq stage, desired reply target, IgnoreList, TX state, or UI priority.

Locked distinction:

> Station identity may be a decoder/search hint; station/QSO policy remains outside `ft8_engine`.

## 12. Hash-store direction

Hashed callsigns are FT8 protocol state and persist across slots.

V3 direction:

```text
ft8_engine
    `-- Ft8HashStore
          explicit storage owner
          context-aware lookup/save
          explicit slot aging
```

No global hash table is required by the message codec. MiniShell does not own this state.

Exact payload comparison is the canonical duplicate identity. A CRC/hash may be used as a quick prefilter but not as a collision-free identity.

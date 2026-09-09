# MiniFT8-V3 RX Architecture and Development Plan

## 1. Milestone

The current MiniFT8-V3 milestone is **decode RX**.

The remaining major radio-domain blocks are intentionally:

```text
RX
AutoSeq
TX
ADIF log
```

RX is developed and understood first. AutoSeq, TX, and ADIF remain outside this milestone.

MiniFT8-V2 is the behavioral/golden reference, not a structure to copy wholesale. V2 code may be kept, cleaned, rewritten, or dropped according to ownership and interface quality. Structural cleanup is always separated from deliberate DSP/algorithm changes.

## 2. Logical RX pipeline

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
        | 6 kHz mono float
        v
[3] rx_slot_framer
        |
        | exact 960-sample FT8 engine blocks
        v
[4] Ft8Engine
        |
        +--> monitor / waterfall
        +--> candidate finder
        +--> candidate decoder / LDPC / CRC
        +--> Ft8HashStore
        `--> typed protocol message codec
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

The monitor, candidate finder/decoder, hash store, and protocol codec are internal pieces of `ft8_engine`. `rx_result_builder` is deliberately outside the protocol engine because it performs station-aware factual normalization/classification.

RX-1B fixed the physical application-level modules as:

```text
rx_audio_adapter
rx_frontend
rx_slot_framer
ft8_engine
rx_result_builder
```

`app_controller` remains the only application coordinator. Canonical RX-1B design: `rx-1b-design.md`.

## 3. Responsibility boundaries

| Block | Owns | Must not own |
| --- | --- | --- |
| `rx_audio_adapter` | MiniShell Audio stream lifecycle and translation into MiniFT8-owned bounded sample blocks | FT8 DSP, WAV/UAC/platform details, channel meaning |
| `rx_frontend` | source/profile channel interpretation and 12 kHz S16/two-channel -> 6 kHz mono-float adaptation state | MiniShell provider behavior, slot timing, FT8 candidate policy |
| `rx_slot_framer` | slot identity, exact 6 kHz sample accounting, bounded partial 960-sample block accumulation | whole-slot raw PCM ownership, FFT, decoding |
| `Ft8Engine` | monitor state/workspace, candidate array, decoder policy, persistent hash store, protocol decode, exact-payload dedupe, current window identity | MiniShell, UI, wall-clock pacing, station/QSO policy, AutoSeq, TX |
| `rx_result_builder` | station-aware factual normalization/classification and `RxBatch` construction | FFT, LDPC, CRC, MiniShell calls, reply/TX policy |

`app_controller` remains the coordinator. RX does not directly call AutoSeq, TX, ADIF, or UI modules behind it.

### Factual classification versus QSO policy

RX may derive factual properties:

```text
is this a CQ?
is it addressed to my station?
is this DXpedition message logically relevant to my station?
```

AutoSeq/application policy owns:

```text
should I reply?
which station should I work?
which TX stage comes next?
should TX be armed?
```

## 4. Streaming and RAM rule

Raw audio is a streaming resource. Normal RX must not require a complete FT8 slot of raw PCM in RAM.

Normal memory classes are:

```text
bounded reusable audio buffers
bounded reusable FFT/workspace memory
whole decode-window waterfall
```

At the MiniShell Audio boundary:

```text
12000 frames/s x 2 channels x 2 bytes = 48000 bytes/s
```

A 15-second raw two-channel slot is about 720 kB, so whole-slot PCM storage is not a normal architectural requirement for Cardputer-class targets.

Locked rule:

> Stream raw audio; retain the waterfall; retain raw PCM only by explicit exception.

### Optional research raw-audio path

Later algorithms may explicitly retain or double-buffer raw slot audio:

```text
                    +--> normal monitor/waterfall/decode
streaming samples --+
                    `--> optional raw capture
                              +--> alternate algorithm A
                              `--> alternate algorithm B
```

This permits meaningful algorithm comparison on identical samples. Linux may enable it; embedded targets may disable it. Normal decode must not depend on it.

## 5. Timing and audio semantics

For live reception:

```text
UTC/sample timestamp establishes initial slot identity/boundary
sample count          measures progress after that
```

For deterministic file/replay tests, samples are the clock; no real-time sleeping is required.

MiniShell transports ordered channel 0/channel 1 data without assigning meaning. MiniFT8 source/profile semantics decide:

```text
ordinary audio
    channel 0/1 -> select/downmix -> FT8 frontend

I/Q
    channel 0 = I
    channel 1 = Q
    -> I/Q processing
```

During ownership/interface cleanup the `ft8_engine` input is deliberately fixed at **6 kHz mono float**, preserving the proven MiniFT8-V2 monitor/decode behavior. MiniShell transport remains 12 kHz/S16/two-channel; `rx_frontend` owns the 12 -> 6 kHz adaptation. Any future 12 kHz engine, filter, FFT, OSR, or search change is a separate measured DSP/algorithm experiment.

## 6. V2 source classification

RX-0 final classification:

| MiniFT8-V2 source | V3 role | Decision |
| --- | --- | --- |
| `main/audio_source.cpp` | old physical RX selector | DROP as V3 transport architecture; reference only |
| `main/stream_uac.cpp` | UAC/device transport | DROP from MiniFT8; below MiniShell Audio |
| `main/stream_mic.cpp` | microphone/device transport | DROP from MiniFT8; below MiniShell Audio |
| `main/resample.cpp` | channel conversion/resampling ideas | REWRITE behind `rx_frontend`; preserve 6 kHz baseline, no filter redesign during cleanup |
| `main/ft8_audio_pipeline.cpp` | mixed RX pipeline | REWRITE/SPLIT; behavior reference only |
| `components/ft8_lib/common/monitor.[ch]` | FFT/waterfall | KEEP math; CLEAN ownership/lifecycle substantially |
| `components/ft8_lib/ft8/decode.[ch]` | candidate + likelihood/LDPC/CRC | KEEP algorithms; CLEAN policy/status/interface details |
| `components/ft8_lib/ft8/message.[ch]` | payload type/unpack/hash mechanics | KEEP protocol mechanics; CLEAN typed representation/hash ownership |
| LDPC/CRC/constants | pure protocol/math core | KEEP unless implementation reveals concrete issue |
| `tests/tx_e2e/decode_helper.cpp` | simplest host reference | KEEP as reference concept; no WAV I/O inside engine |

TX-side message encoding cleanup is deferred to the TX milestone.

## 7. Decoder and protocol rules

### V2 decode profile baseline

During structural cleanup:

```text
sample rate            6000 Hz
f_min                    200 Hz
f_max                   2900 Hz
time_osr                    2
freq_osr                    1
candidate capacity          50
minimum sync score           5
max LDPC iterations         25
```

The current V2 time-search range is also preserved first. These are explicit profile/search policy, not FT8 protocol constants.

Candidate score is a synchronization score, not SNR.

### SNR

V2's chosen SNR estimator is the initial baseline. Preserve it during structural cleanup; improve/calibrate it later only as a separate algorithm experiment.

### Message type and structure

Protocol message type is first-class. Normal typed messages must not be decoded to text and then tokenized again to rediscover structure.

The cleaned protocol result is now represented by `Ft8ProtocolMessage`:

```text
Ft8ProtocolMessage
    exact payload identity
    protocol type
    parse status
    typed message-specific fields
    canonical rendered text
    unresolved-hash fact
    candidate/LDPC/CRC diagnostics
```

Known protocol type and structured-unpack support are separate facts. RX-1F implements this representation for the V2-supported receive families.

### Free-text CQ exception

A protocol `FREE_TEXT` message remains `FREE_TEXT`, but `rx_result_builder` may set logical `is_cq=true` when canonical text matches exactly:

```text
CQ <nnn|AAAA> <valid-callsign> [valid-grid]
```

This is an explicit classifier, not a return to generic rendered-text parsing.

### Deep-search context

Future reply-to-me deep search may accept explicit station-aware decoder context such as the local callsign. It may influence candidate generation/ranking or prior-assisted likelihood/LDPC decoding.

It must not carry:

```text
AutoSeq stage
desired reply target
IgnoreList
TX state
UI priority
```

Locked distinction:

> Station identity may be a decoder/search hint; station/QSO policy remains outside `ft8_engine`.

### Hash store

Hashed callsigns are FT8 protocol state across slots:

```text
Ft8Engine
    `-- Ft8HashStore
          explicit per-engine owner
          context-aware lookup/save
          explicit aging
```

The pinned V2 production behavior remains:

```text
capacity                     128 entries
entry                         16 bytes
stored hash                  full 22-bit hash
lookup widths                22 / 12 / 10 bits
bucket                       (top10 * 23) % 128
age                          uint8, saturating
save/lookup                  refresh age to zero
trim-created holes           scan full table on lookup
full-table policy            trim to 78, then insert -> 79
```

RX-1G establishes the actual aging owner. The first decode window does not age an empty/new store. Beginning a new window after a completed window calls `ft8_hash_store_age_slot()` exactly once. An aborted window caused by stream reset is not counted as a completed slot for aging.

RX-1F connects 22-, 12-, and 10-bit protocol lookups directly to this store. A missing hash remains a valid parsed message rendered as `<...>` and additionally carries `has_unresolved_hash=true`.

No global hash table is required by the codec. MiniShell does not own this state.

## 8. Golden-reference policy

Canonical RX-1A record: `rx-golden.md`.

Primary structural invariant:

```text
same input
    -> same reference-host waterfall
    -> same valid exact payloads
    -> same supported protocol type/semantics
```

Candidate score/order, V2 SNR, CRC quick hash, and V2 field offsets are diagnostic rather than hard permanent contracts.

Structural changes and algorithm changes are isolated:

```text
A  ownership/global cleanup       -> same decoder behavior
B  interface/module cleanup       -> same decoder behavior
C  targeted protocol bug fix      -> dedicated vector/test
D  resampling/FFT change          -> deliberate comparison
E  OSR/deep-search/SNR change     -> deliberate comparison
```

Do not automatically rewrite golden values because a refactor fails a test. Investigate the difference first.

RX-1C reinforced this rule: a mathematically equivalent reassociation in Hann-window float multiplication changed the compact waterfall fingerprint. Restoring V2's exact float operation order restored the hard golden. Structural DSP cleanup therefore preserves expression-level numerical behavior where the golden proves it matters.

## 9. Development stages

### RX-0 — architecture and V2 source review — complete

Canonical reviews:

```text
rx-decoder-contract.md
rx-v2-production-review.md
rx-monitor-review.md
rx-decode-review.md
rx-message-review.md
```

### RX-1 — clean FT8 decode core

#### RX-1A — freeze golden boundaries — complete

Pinned V2 algorithm baseline:

```text
5bd3ef98f72388a850bebad04bd7300b90edb63c
```

Hard Linux reference anchors include:

```text
FT8 active-waterfall FNV-1a-64  18BE1E838FD9C6AF
FT4 active-waterfall FNV-1a-64  CBDD2509276E5030
unique CQ payload                000000206016500A1988
```

Fixed protocol vectors cover standard CQ, ARRL Field Day, DXpedition, non-standard CQ, and a FREE_TEXT CQ-shaped message. Existing V2 `golden_rx` plus the boundary regression pass in CI.

Known V2 behaviors/limitations outside the structural golden remain explicitly documented in `rx-golden.md` and the RX source reviews; RX-1 does not silently expand protocol scope or alter decoder math while cleaning ownership.

#### RX-1B — top-down module/interface design — complete

Canonical design:

```text
rx-1b-design.md
```

RX-1B fixed:

```text
five physical RX modules
single owner for every mutable RX resource/state
12 kHz MiniShell transport -> 6 kHz engine boundary
960-sample FT8 engine block contract
slot/window and stream-discontinuity semantics
caller-allocated / engine-logically-owned workspace
raw ft8_lib types private inside ft8_engine
dependency direction
error/status distinctions
unit-test responsibility by boundary
```

No V2 decoder source was migrated during RX-1B.

#### RX-1C — clean monitor ownership/workspace/lifecycle — complete

Canonical record:

```text
rx-1c-monitor.md
```

RX-1C implemented an explicit `Ft8Monitor` instance with a queryable caller-supplied workspace. The monitor owns no allocator and has no mutable DSP singleton.

Baseline host reference storage is approximately:

```text
FFT plan               9888 bytes
waterfall              80538 bytes
window/history/scratch 15368 bytes
alignment/padding         14 bytes
---------------------------------
total                 105808 bytes
```

The exact total is queried rather than hard-coded and may differ slightly on a 32-bit target.

RX-1C unit/golden tests prove exact V2-compatible dimensions, two independent monitor instances, explicit invalid/workspace/full status, new-window history preservation, stream-reset history clearing, safe destruction, and byte-identical RX-1A active FT8 waterfall:

```text
FNV-1a-64 = 18BE1E838FD9C6AF
```

No candidate-search/LDPC/message code was migrated in RX-1C.

#### RX-1D — candidate search + likelihood/LDPC/CRC — complete

Canonical record:

```text
rx-1d-decoder.md
```

RX-1D migrated the pinned V2 FT8 candidate/decode path behind `ft8_engine`:

```text
Ft8WaterfallView
    -> candidate search
    -> max-log likelihoods
    -> BP-LDPC
    -> CRC-14
    -> Ft8DecodedPayload
```

Locked policy remains:

```text
candidate capacity      50
minimum sync score       5
max LDPC iterations     25
time search             -10..19 blocks
```

The boundary returns exact validated payload bytes and decoder diagnostics. It deliberately does not depend on V2 `ftx_message_t`, callsign hashing, message rendering, MiniShell, UI, AutoSeq, or TX.

Two structural cleanups are now regression-proven:

- negative candidate time offsets use absolute in-array waterfall addressing rather than forming a pointer before the array;
- the LDPC reverse adjacency is reconstructed deterministically from one canonical parity-check topology rather than storing duplicate forward/reverse tables.

Unit and pinned-V2 golden tests pass. Exact RX-1A payload remains:

```text
000000206016500A1988
```

Candidate score/order remain diagnostics and are not made permanent golden identity.

#### RX-1E — explicit per-engine Ft8HashStore — complete

Canonical record:

```text
rx-1e-hash-store.md
```

RX-1E replaces V2's process-global/static callsign table with an explicit caller-owned `Ft8HashStore` suitable for one future `Ft8Engine` instance.

The implementation preserves:

```text
128-entry capacity
16-byte compact entries
full 22-bit canonical stored hash
22 / 12 / 10-bit lookup semantics
(top10 * 23) % 128 bucket mapping
full-table lookup through trim-created holes
save/lookup age refresh
uint8 saturating slot age
same-full-hash callsign replacement
V2 128 -> 78 -> 79 full-table policy
```

`age_slot()` makes cross-slot lifetime explicit instead of depending on V2's global slot guard. The store has no MiniShell, clock, platform, UI, AutoSeq, or TX dependency.

Unit tests prove two-store independence, all three lookup widths, collision replacement, age refresh, trim-hole probing, full-table eviction behavior, and invalid-input handling. Linux and the RX-1C/RX-1D regressions remain green.

#### RX-1F — typed protocol message codec + Ft8ProtocolSlot — complete

Canonical record:

```text
rx-1f-message-codec.md
```

RX-1F implements:

```text
Ft8DecodedPayload
    -> protocol type classification
    -> typed structured unpacking
    -> Ft8HashStore lookup/save
    -> canonical protocol text
    -> Ft8ProtocolMessage
    -> exact-payload dedupe
    -> Ft8ProtocolSlot
```

The canonical representation is typed; rendered text is derived convenience. Protocol type and parse status are separate. V2-supported RX families remain `STANDARD`, `NONSTD_CALL`, `FREE_TEXT`, `DXPEDITION`, `ARRL_FD`, and `TELEMETRY`; recognized but unimplemented families remain explicit `UNSUPPORTED`, and type `0.6` remains `UNKNOWN`.

All five frozen RX-1A codec vectors reproduce their exact canonical text. The codec resolves/saves callsigns through the explicit RX-1E store with real 22/12/10-bit lookup semantics. Hash misses remain valid protocol parses using `<...>` plus explicit unresolved state.

`Ft8ProtocolSlot` uses caller-supplied `Ft8ProtocolMessage[]` storage and exact 10-byte payload comparison for dedupe; no hidden allocator or slot-sized message array is introduced.

The RX-1F code-bearing head passes Linux, RX-1C pinned waterfall regression, and RX-1D pinned payload regression.

#### RX-1G — pure cleaned Ft8Engine golden regression — complete

Canonical record:

```text
rx-1g-engine.md
```

RX-1G assembles the cleaned RX-1C through RX-1F pieces behind one explicit pure engine owner:

```text
6 kHz mono float
    -> Ft8Engine
       -> Ft8Monitor
       -> candidate search
       -> likelihood / LDPC / CRC
       -> Ft8HashStore
       -> typed message codec
       -> exact-payload dedupe
    -> Ft8ProtocolSlot
```

The engine is caller-owned and has no allocator or MiniShell dependency. Its external workspace remains the queried monitor workspace; candidate storage and the compact hash store are fixed state inside the engine instance and are reported separately by `Ft8EngineRequirements`.

Lifecycle is explicit:

```text
query requirements
init
begin_window(slot_id)
process exact 960-sample blocks
finalize_window -> Ft8ProtocolSlot
reset_stream on discontinuity
repeat
destroy
```

A new window cannot begin while another is active. A successful slot with no valid messages returns `FT8_ENGINE_NO_MESSAGES`. `reset_stream()` cancels the active window and clears monitor continuity while preserving protocol/hash knowledge.

The dedicated RX-1G pinned-V2 golden passes:

```text
ft8_cq_w1xyz_fn42.wav
    -> Ft8Engine
    -> one message
       payload = 000000206016500A1988
       type    = STANDARD
       parse   = OK
       text    = CQ W1XYZ FN42
```

The RX-1G lifecycle/unit test and normal Linux suite pass as well.

### RX-2 — pure host FT8 decoder/use harness — NEXT

```text
known 6 kHz engine-native PCM
    -> completed Ft8Engine
    -> typed protocol messages
```

RX-2 should make the pure cleaned engine easy to use and test as a host-side decoder without MiniShell, UI, station-aware result building, AutoSeq, or TX.

### RX-3 — RX frontend

```text
12 kHz / S16 / 2-channel
    -> MiniFT8 source/profile semantics
    -> select/downmix
    -> 6 kHz mono float engine-native stream
```

Preserve baseline behavior first; filtering/resampling redesign is outside this structural stage.

### RX-4 — streaming slot framing

Verify exact 6 kHz slot/sample accounting, 960-sample engine blocks, boundary behavior, partial-first-slot handling, no lost/duplicated samples, explicit new-window versus stream-discontinuity reset behavior, and bounded memory.

### RX-5 — pure RX assembly

```text
bounded PCM
    -> rx_frontend
    -> rx_slot_framer
    -> Ft8Engine
    -> Ft8ProtocolSlot
    -> rx_result_builder
    -> RxBatch
```

### RX-6 — MiniShell WAV Audio integration

```text
tests/kfs16b12k.wav
    -> MiniShell WAV Audio
    -> rx_audio_adapter
    -> RX
    -> RxBatch
```

Changing WAV to a future QMX provider must not alter the RX domain pipeline.

### RX-7 — real decoded RX UI

```text
RxBatch
    -> app_controller
    -> UiModel
    -> ui_shell
```

At that point stop the RX milestone. AutoSeq, TX, and ADIF remain separate major blocks.

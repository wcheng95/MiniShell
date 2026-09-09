# MiniFT8-V3 RX-1B — Top-Down Module and Ownership Design

Status: **LOCKED / COMPLETE**

RX-1B defines the receive-side module boundaries, ownership, data contracts, lifecycle, memory model, dependency direction, error semantics, and test boundaries before any MiniFT8-V2 decoder source is migrated.

The governing rule is:

> Preserve the proven MiniFT8-V2 FT8 receive algorithm first. RX-1B and the first implementation stages are ownership/interface cleanup, not performance or decoder-algorithm work.

## 1. Locked receive path

```text
MiniShell Audio
12 kHz / S16 / channel 0..1
        |
        v
rx_audio_adapter
        |
        | MiniFT8-owned bounded interleaved S16 frames
        v
rx_frontend
        |
        | 6 kHz mono float engine samples
        v
rx_slot_framer
        |
        | exact 960-sample FT8 engine blocks
        | + explicit slot/window events
        v
ft8_engine
        |
        | Ft8ProtocolSlot
        v
rx_result_builder
        |
        | RxBatch
        v
app_controller
        |
        v
UI / future AutoSeq
```

`app_controller` remains the only application coordinator. No second RX manager/coordinator is introduced.

## 2. Locked 6 kHz engine boundary

MiniShell Audio transport remains:

```text
12000 Hz
S16
2 ordered channels
```

The cleaned FT8 engine boundary is explicitly:

```text
6000 Hz
mono
float samples
960 samples per FT8 monitor block
```

This preserves the proven V2 monitor/decode configuration during structural cleanup:

```text
sample_rate            6000 Hz
f_min                    200 Hz
f_max                   2900 Hz
time_osr                    2
freq_osr                    1
FT8 monitor block_size    960 samples
candidate capacity          50
minimum sync score           5
max LDPC iterations         25
```

The 6 kHz rate is a **current FT8-engine contract**, not a MiniShell Audio contract and not a claim that 6 kHz is permanently optimal.

Any future change to 12 kHz engine processing, OSR, filtering, FFT sizes, search policy, SNR estimation, deep search, or decoder math is a separate measured DSP/algorithm change after the ownership cleanup is stable.

## 3. Physical module boundaries

RX-1B chooses five application-level modules.

```text
apps/ft8/src/
    rx_audio_adapter/
    rx_frontend/
    rx_slot_framer/
    ft8_engine/
    rx_result_builder/
```

The internal `ft8_engine` implementation may contain several private files, but they are **not application-wide modules**:

```text
ft8_engine/
    ft8_engine.[ch]          public engine edge inside MiniFT8
    ft8_monitor.[ch]         private PCM -> waterfall implementation
    ft8_candidate.[ch]       private search + candidate decode
    ft8_message_codec.[ch]   private protocol unpack/render
    ft8_hash_store.[ch]      private explicit persistent protocol state
```

Exact private filenames may change during migration if a smaller arrangement is clearer. The ownership boundaries may not.

## 4. Ownership table

| Owner | Owns | Does not own |
| --- | --- | --- |
| MiniShell Audio | provider/device transport, native conversion below API, stream cleanup | FT8 channel meaning, 6 kHz adaptation, slot framing, DSP |
| `rx_audio_adapter` | MiniShell RX stream handle and Audio API lifecycle; bounded transport read buffer; translation of API errors/end-of-stream | device driver, WAV parser, channel interpretation, FT8 DSP |
| `rx_frontend` | channel selection/downmix policy for selected RX profile; 12 kHz S16 -> 6 kHz mono-float adaptation state | MiniShell stream handle, UTC/slot state, waterfall, decode policy |
| `rx_slot_framer` | FT8 slot identity; exact 6 kHz sample accounting; bounded partial 960-sample block accumulator; decode-window begin/finalize events | clocks/devices, FFT, candidate search, whole-slot PCM |
| `ft8_engine` | monitor state; waterfall; candidate array; likelihood/LDPC/CRC scratch; protocol decode state; `Ft8HashStore`; exact payload dedupe | MiniShell API, files, UTC, UI, AutoSeq, TX, ADIF, radio control |
| `rx_result_builder` | station-aware factual classification and conversion `Ft8ProtocolSlot -> RxBatch` | DSP, MiniShell, TX/reply decisions |
| `app_controller` | top-level RX start/stop sequencing; module instances; station context; delivery of `RxBatch` to UI/future policy | driver internals or private DSP buffers |

### Failure containment rule

Each owner must be destructible independently from a partially initialized state. A failure below one boundary is reported upward; callers do not reach through a failed module to repair its private resource.

## 5. Dependency direction

Allowed dependencies:

```text
app_controller
    |
    +--> rx_audio_adapter ------> MiniShell public Audio/Time APIs
    +--> rx_frontend
    +--> rx_slot_framer
    +--> ft8_engine
    `--> rx_result_builder

rx_result_builder ------> MiniFT8 RX/protocol types only
rx_slot_framer ---------> MiniFT8 RX types only
rx_frontend ------------> MiniFT8 RX types only
ft8_engine  ------------> private FT8 math/protocol implementation only
```

Forbidden dependencies:

```text
ft8_engine -> MiniShell
ft8_engine -> UI
ft8_engine -> app_controller
ft8_engine -> storage_service
ft8_engine -> qso_scheduler
ft8_engine -> platform/ESP-IDF/M5/POSIX

rx_frontend -> MiniShell Audio
rx_slot_framer -> MiniShell Time
rx_result_builder -> MiniShell

any RX module -> AutoSeq/TX/ADIF policy
```

Only `rx_audio_adapter` is allowed to include/use MiniShell Audio types. `app_controller` may use MiniShell Time to establish the initial sample/slot reference and pass ordinary MiniFT8-owned timing values into `rx_slot_framer`.

## 6. Cross-boundary data contracts

The implementation will use MiniFT8-owned types. Raw `ft8_lib` types remain private to `ft8_engine`.

### 6.1 Transport block

`rx_audio_adapter` produces a bounded block conceptually containing:

```text
RxTransportBlock
    frame_count
    interleaved S16 samples [frame][channel]
```

Contract:

- exactly 12 kHz / S16 / two ordered channels for the current MiniFT8 profile;
- no stereo/IQ meaning is assigned by MiniShell or the adapter;
- arbitrary provider read chunk sizes are accepted;
- no whole-slot transport buffer.

### 6.2 Engine-native sample stream

`rx_frontend` emits:

```text
RxEngineSamples
    float samples[]
    sample_count
```

Contract:

```text
sample rate = 6000 Hz
one logical FT8 receive stream
normalized float representation compatible with V2 monitor input
ordered, continuous samples
```

The frontend owns any fractional/decimation carry needed across transport blocks. Transport block boundaries must not be visible to the engine.

### 6.3 Slot framer events

`rx_slot_framer` consumes the 6 kHz stream and exposes only complete 960-sample FT8 monitor blocks plus explicit lifecycle events:

```text
BEGIN_WINDOW(slot_id)
ENGINE_BLOCK(slot_id, 960 samples)
FINALIZE_WINDOW(slot_id)
STREAM_RESET
```

The exact C representation may be callbacks or returned events; the semantics are locked.

The framer owns the bounded partial-block accumulator. It never owns full-slot PCM.

### 6.4 Engine output

The engine returns one completed slot result:

```text
Ft8ProtocolSlot
    slot_id
    decode status
    message_count
    messages[]
```

Each `Ft8ProtocolMessage` preserves at least:

```text
exact payload bytes / identity
protocol message type
parse/structured-decode status
canonical rendered text
candidate sync score          diagnostic
frequency offset
time offset
V2-baseline SNR
optional LDPC/CRC diagnostics
message-specific structured fields
```

Exact payload bytes, not CRC or a quick hash, are authoritative message identity.

The engine returns zero or more unique valid messages; it does not stop after the first successful decode.

### 6.5 Application RX result

`rx_result_builder` converts protocol results into factual application results:

```text
RxBatch
    slot_id
    message_count
    RxMessage[]
```

`RxMessage` may add facts such as:

```text
is_cq
is_to_me
resolved callsign/grid fields
DXpedition relevance to local station
```

It must not add policy such as:

```text
should_reply
selected_station
next_tx_stage
TX armed
UI priority
```

Those remain future application/AutoSeq responsibilities.

## 7. Slot timing contract

For live RX:

```text
UTC/sample timestamp establishes initial slot identity and phase
sample count owns progress after that
```

`ft8_engine` never reads a clock.

`rx_slot_framer` never calls MiniShell Time directly. `app_controller` establishes the stream-start timing reference using MiniShell Time and passes ordinary timing values to the framer.

The first partial slot after a stream start/discontinuity is not treated as a complete decode window. The framer advances by sample count to the next complete boundary.

Normal decode-window transition and stream discontinuity are different operations:

```text
begin_new_decode_window
    reset waterfall/decode-window state
    preserve monitor analysis history

reset_stream
    reset waterfall/decode-window state
    clear monitor analysis history
```

This preserves the V2 monitor-history behavior while making discontinuity explicit.

## 8. Engine lifecycle

Conceptual lifecycle:

```text
query workspace requirements
        |
        v
caller allocates workspace
        |
        v
engine_init(config, workspace)
        |
        +--> begin_new_decode_window
        |       |
        |       +--> process exact 960-sample blocks
        |       |
        |       `--> finalize -> Ft8ProtocolSlot
        |
        +--> repeat windows
        |
        +--> reset_stream when continuity is lost
        |
        v
engine_destroy
        |
caller frees workspace
```

Initialization must either succeed completely or leave the instance safely destructible.

## 9. Memory/workspace ownership

### Caller allocation, engine logical ownership

`ft8_engine` exposes a deterministic workspace-requirement query for a selected decode profile.

The top-level MiniFT8 composition obtains the requested memory through MiniShell Memory and gives the engine ordinary pointer/size storage. The engine never calls MiniShell Memory itself.

While initialized, `ft8_engine` exclusively owns the logical use of its assigned workspace.

Initial V2-compatible FT8 monitor memory is expected to be dominated by:

```text
waterfall                    ~80.5 kB
FFT plan/work                 ~12.0 kB
window/history/scratch        ~15 kB
candidate/decode workspace    additional bounded storage
```

The exact requirement will be measured by the cleaned implementation rather than guessed or hidden in globals.

### Workspace classes

The query should make these lifetimes visible even if V1 uses one allocation:

```text
persistent engine state
waterfall storage
scratch workspace
candidate/result storage
```

This leaves open later safe reuse of scratch without changing ownership boundaries.

### No mutable DSP singleton

Mutable DSP state may not live in file-static/global buffers. Two separately initialized engines with separate workspaces must be independent.

`Ft8HashStore` is an exception only in lifetime, not ownership: it persists across decode windows, but it is explicitly owned by one `ft8_engine` instance.

## 10. Error/status contracts

Each module reports errors in its own domain and leaves cleanup possible.

Required categories:

### `rx_audio_adapter`

```text
OK / frames available
TIMEOUT / no frames yet
END_OF_STREAM
NOT_READY
IO_ERROR
INVALID_FORMAT
```

### `rx_frontend`

```text
OK
INVALID_CONFIG
INVALID_INPUT
OUTPUT_CAPACITY
```

### `rx_slot_framer`

```text
NEED_MORE
BLOCK_READY
WINDOW_READY
INVALID_TIMING
DISCONTINUITY
```

### `ft8_engine`

```text
OK
INVALID_CONFIG
INSUFFICIENT_WORKSPACE
NOT_INITIALIZED
WATERFALL_FULL
DECODE_COMPLETE
DECODE_FAILED / no valid messages
INTERNAL_ERROR
```

A no-decode slot is normal data-plane behavior, not a platform failure.

Error enums may be consolidated during implementation, but these distinctions may not be collapsed into a single boolean success/failure where the caller needs different recovery.

## 11. Unit-test boundaries

RX implementation proceeds only with tests at the ownership boundaries.

### `rx_audio_adapter`

- exact MiniShell Audio lifecycle: open/start/read/stop/close;
- timeout versus end-of-stream distinction;
- arbitrary transport chunk sizes;
- cleanup after partial initialization/failure;
- no provider-specific assumptions.

### `rx_frontend`

- deterministic 12 kHz S16 two-channel -> 6 kHz float output;
- channel/profile semantics separated from transport;
- chunk-boundary independence;
- no hidden static conversion state;
- reset/discontinuity behavior.

### `rx_slot_framer`

- exactly 960 engine samples per monitor block;
- no sample loss/duplication across arbitrary input chunks;
- first partial slot handling;
- slot wrap/finalization from sample count;
- normal new-window versus stream-reset distinction;
- bounded accumulator only.

### `ft8_monitor`

- derived V2-compatible dimensions;
- exact RX-1A waterfall bytes/hash;
- two-instance independence;
- insufficient-workspace/invalid-config failure;
- window reset preserves history;
- stream reset clears history;
- full waterfall reported explicitly.

### candidate/decode core

- exact RX-1A unique payload regression;
- V2 profile 50 / 5 / 25 preserved;
- multiple-message collection and exact-payload dedupe;
- candidate score remains distinct from SNR.

### message codec / hash store

- fixed RX-1A protocol vectors;
- type and canonical text preserved;
- explicit 22/12/10-bit hash lookup behavior;
- multiple engine/hash-store instances independent;
- explicit aging behavior.

### `rx_result_builder`

- factual CQ/to-me classification;
- FREE_TEXT CQ-shaped exception;
- no reply/TX policy fields or decisions.

### integration/golden

- RX-1A FT8 waterfall FNV-1a-64 `18BE1E838FD9C6AF`;
- unique CQ payload `000000206016500A1988`;
- MiniShell `tests/kfs16b12k.wav` eventually enters only through Audio API;
- platform-boundary test continues rejecting platform headers under `apps/ft8/`.

## 12. Implementation sequence after RX-1B

The next steps are now fixed by dependency order rather than by V2 file layout:

```text
RX-1C  clean monitor instance + explicit workspace/lifecycle
RX-1D  candidate search + likelihood/LDPC/CRC behind ft8_engine
RX-1E  explicit per-engine Ft8HashStore
RX-1F  typed protocol message codec + Ft8ProtocolSlot
RX-1G  pure cleaned ft8_engine golden regression

RX-2   pure MiniShell-independent host decoder assembly
RX-3   12 kHz S16/2ch -> 6 kHz rx_frontend
RX-4   sample-count slot framer
RX-5   pure RX assembly + rx_result_builder
RX-6   MiniShell WAV Audio integration
RX-7   real decoded RX UI
```

The stages may be split into smaller commits/tests, but deliberate DSP optimization does not enter these stages unless a proven structural blocker forces a narrowly documented exception.

## 13. Explicit deferred work

Do not mix into RX-1C through RX-1G:

```text
12 kHz engine conversion
freq_osr/time_osr improvement
larger/different FFT
candidate-ranking improvement
deep search
SNR redesign
filter redesign
raw-slot retention/time-domain subtraction
performance tuning solely for speed
RAM optimization solely for footprint
```

RAM will be measured throughout. Ownership cleanup may naturally remove waste, but optimization is not the objective.

## 14. RX-1B exit decision

RX-1B is complete because the design now fixes:

```text
[done] top-level responsibilities
[done] physical module boundaries
[done] single owner for every mutable RX resource/state
[done] MiniFT8-owned cross-boundary data semantics
[done] 12 kHz MiniShell -> 6 kHz engine boundary
[done] slot/window lifecycle and discontinuity semantics
[done] workspace allocation versus logical ownership
[done] dependency direction
[done] error/status distinctions
[done] unit-test ownership
[done] implementation order
```

No production decoder implementation was migrated during RX-1B.

Next active stage: **RX-1C — clean monitor ownership/workspace/lifecycle while preserving byte-identical RX-1A waterfall behavior.**

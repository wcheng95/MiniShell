# MiniFT8-V3 RX Decoder Contract — RX-0B

## 1. Purpose

This document records the first source review for RX-0B. The reference is MiniFT8-V2:

```text
tests/tx_e2e/decode_helper.cpp
```

The goal is **not** to copy this helper into V3. The goal is to identify the smallest clean decoder contract hidden inside it, then use MiniFT8-V2 production behavior as the golden reference while the implementation is cleaned.

## 2. Why this file is the first reference

`decode_helper.cpp` exposes the essential FT8 path without USB, FreeRTOS scheduling, UI, CAT, or AutoSeq:

```text
PCM
    -> monitor
    -> candidate search
    -> candidate decode
    -> message decode
```

That makes it much easier to reason about than V2 `main.cpp` or `ft8_audio_pipeline.cpp`.

However, the helper is a **test utility**, not a production decoder contract. Several pieces must be separated before any code is brought into V3.

## 3. Section-by-section review

### 3.1 Headers and dependencies

The helper directly includes:

```text
ft8/decode.h
common/monitor.h
```

plus C++ test utilities such as `std::vector`, `std::map`, file I/O, and strings.

V3 conclusion:

- the FT8 math/decoder dependency is legitimate inside `ft8_engine`;
- C++ containers and host file I/O are test-harness choices, not engine requirements;
- raw `ft8_lib` types should remain private to `ft8_engine` where practical rather than becoming application-wide MiniFT8 types.

### 3.2 Callsign-hash test state

The helper owns a process-global `std::map<uint32_t, std::string>` and adapts it to `ftx_callsign_hash_interface_t` callbacks.

The callback concept is useful: FT8 message unpacking legitimately needs hash lookup/save behavior for hashed callsigns.

The ownership is not suitable for V3:

```text
V2 helper
    global map
        -> callback interface

V3 direction
    explicit decoder/hash-store state
        -> callback/interface used by message codec
```

The hash store must have a visible owner and lifecycle. It must not be an unexplained global hidden in a decoder test helper.

Whether the hash store belongs inside `ft8_engine` or in a small MiniFT8-owned service adjacent to it will be decided after reviewing V2 production hash behavior. The important boundary is already clear: it is domain state, not MiniShell state.

### 3.3 WAV writer

`write_wav()` writes a mono S16 WAV for test diagnostics.

V3 decision: **test utility only**.

It does not belong inside `ft8_engine`, RX frontend, or MiniShell-facing RX code. A later optional raw-audio research/capture feature may write WAV through an appropriate test/tool path, but that is separate from decoding.

### 3.4 WAV reader

`read_wav()` parses a host file and loads the entire mono WAV into `std::vector<float>`.

V3 decision: **test harness only**.

Loading the whole WAV into host RAM is acceptable for a deterministic unit/golden test. Production RX remains streaming and must not require whole-slot PCM storage.

The MiniShell integration path is already different:

```text
WAV file
    -> MiniShell WAV Audio provider
    -> streaming Audio ABI frames
    -> MiniFT8 RX
```

Therefore no WAV parser belongs in the V3 decoder engine.

### 3.5 Text normalization

`normalize_text()` uppercases and normalizes whitespace for test comparison.

V3 decision: keep this concept in golden-test tooling unless later application behavior proves that an equivalent canonicalization belongs in `message/result builder`.

The raw protocol/message codec and the golden comparison layer should not be conflated merely because the helper contains both.

### 3.6 `decode_pcm()` — the useful core

The important helper chain is:

```text
monitor_config
    -> monitor_init/reset
    -> monitor_process() once per complete block
    -> ftx_find_candidates()
    -> ftx_decode_candidate()
    -> ftx_message_decode()
    -> result
```

This is the conceptual seed for V3.

#### Configuration

The helper explicitly sets:

```text
f_min       = 200 Hz
f_max       = 2900 Hz
sample_rate = input sample rate
 time_osr   = 2
freq_osr    = 1
protocol    = FT8/FT4
```

The 6 kHz / `freq_osr=1` combination is partly constrained by V2 `monitor.c`'s static 960-point FFT limit. V3 must not mistake that implementation constraint for a permanent protocol requirement.

Initial V3 golden tests may deliberately reproduce these values while structural cleanup is underway. Any later change to internal sample rate, OSR, frequency window, candidate limit, LDPC policy, or search strategy is a separate measured algorithm decision.

#### Monitor initialization

The helper calls `monitor_init()` and then `monitor_reset()`.

Current V2 `monitor_init()` returns `void`, even though its implementation can encounter allocation/FFT setup failures.

V3 requirement:

> Initialization failure must be observable through an explicit result/status. A partially initialized monitor must never look like a valid decoder instance.

#### Streaming blocks

The helper iterates over the already-loaded PCM array in exact `mon.block_size` chunks and calls `monitor_process()`.

This proves an important property:

> The FT8 monitor itself does not fundamentally require a whole-slot PCM buffer; it can build the whole-slot waterfall incrementally from bounded sample blocks.

That aligns with the V3 streaming/RAM rule.

The helper silently ignores any trailing partial block. V3 must make partial-block/slot-finalization behavior explicit rather than depending on a loop condition.

#### Candidate search

The helper uses:

```text
max candidates = 20
min score      = 0
```

This is useful for tests but is **not** MiniFT8-V2 production policy.

Production V2 currently uses:

```text
max candidates = 50
min score      = 5
```

Therefore candidate capacity and threshold must be explicit decoder policy/configuration, not hidden constants inside a helper.

Future deep-search algorithms may also accept an explicit optional station search context such as the local callsign. That context is an algorithmic hint for candidate recovery only; it is not QSO or AutoSeq policy.

#### Candidate decode

The helper tries candidates in returned order using:

```text
max LDPC iterations = 4
```

and stops at the first successful decode.

Production MiniFT8-V2 uses up to 25 iterations and continues to collect multiple unique decodes.

V3 requirement:

> One slot decode returns zero or more unique messages. The helper's first-success behavior must not become the V3 engine contract.

LDPC iteration limit is explicit decode policy, not an accidental hard-coded property.

#### Message decode

A successfully decoded payload is passed to `ftx_message_decode()` with the hash interface to produce message type, text, and field offsets.

This is a clean conceptual boundary:

```text
candidate decoder
    -> protocol payload + decode status

message codec
    -> protocol message type + structured/message text + field metadata
```

V3 should preserve that separation and preserve the protocol message type as first-class output so ordinary messages do not need to be re-tokenized from rendered text.

A deliberate application-level exception exists for protocol `FREE_TEXT`: the RX result classifier may recognize the exact valid-CQ grammar documented in `rx-v2-production-review.md` and set logical CQ classification without changing the protocol type.

#### Misnamed SNR

The helper assigns:

```text
result.snr = candidates[i].score
```

That value is a synchronization/candidate score, **not SNR**.

V3 must keep these concepts distinct:

```text
candidate_score
signal/SNR estimate
frequency offset
time offset
LDPC/CRC status
```

Production V2 has a separate chosen SNR estimator. V3 preserves that estimator as the structural-cleanup baseline and may improve it later as an intentional algorithm change.

## 4. Helper versus production V2

The helper is the best starting point for understanding structure, but production V2 remains the behavioral/golden reference.

Known differences already identified:

| Policy | V2 helper | V2 production |
| --- | ---: | ---: |
| candidate capacity | 20 | 50 |
| minimum sync score | 0 | 5 |
| max LDPC iterations | 4 | 25 |
| decoded messages returned | first success only | multiple unique messages |
| duplicate handling | none after first success | payload/hash dedupe |
| `snr` field | actually candidate score | separate V2 SNR estimator |

RX-0 golden cases must therefore be generated/validated against production V2 behavior, not merely against `decode_helper.cpp`.

## 5. V3 ownership boundary

The seven logical RX blocks remain:

```text
rx_audio_adapter
rx_frontend
rx_slot_framer
ft8_monitor
candidate finder
candidate decoder
message/result builder
```

For implementation ownership, blocks 4-7 naturally form the internal FT8 engine:

```text
RX
|
+-- rx_audio_adapter        MiniShell-facing edge
+-- rx_frontend             channel/source-profile DSP
+-- rx_slot_framer          slot/sample accounting
`-- ft8_engine
      +-- monitor/waterfall
      +-- candidate search
      +-- candidate decode / LDPC / CRC
      `-- message codec / protocol decode-result construction
```

`ft8_engine` is a MiniFT8 domain module. It must have no MiniShell, file, platform, UI, AutoSeq, TX, or ADIF dependency.

Raw `ft8_lib` structures should preferably remain behind this engine boundary. The rest of MiniFT8 should consume MiniFT8-owned RX result types.

The engine's **decode semantics** remain protocol-level. A future search algorithm may receive explicit station-aware hints such as the local callsign when those hints improve decoding. This does not move station/QSO policy into the engine.

## 6. Decoder contract — properties, not final C API

We are **not locking final function names or structs yet**. The contract currently requires these capabilities.

### Monitor lifecycle

```text
create/init monitor with explicit configuration
query required input block size / workspace requirements
reset/start a slot
feed bounded engine-native sample blocks incrementally
finalize a slot
obtain a read-only finalized waterfall
release monitor/workspace
```

Properties:

- all mutable state has an explicit owner;
- no hidden singleton FFT/window/waterfall buffers;
- initialization failure is explicit;
- memory requirements are inspectable;
- normal processing does not require whole-slot raw PCM.

### Candidate search

Input:

```text
finalized waterfall
explicit search policy
optional algorithmic search context
```

Output:

```text
0..N candidate descriptors
```

A candidate descriptor contains synchronization/location information only. It does not contain application/QSO policy.

A future search context may conceptually contain:

```text
deep_search_enabled
local_callsign (optional)
```

and may later grow other explicit decoder hints. It must not contain AutoSeq state, desired reply target, TX stage, IgnoreList, or UI preference.

### Candidate decode

Input:

```text
waterfall
candidate
explicit LDPC/decode policy
```

Output:

```text
success/failure
payload
frequency/time decode status
LDPC/CRC diagnostics
```

### Message decode

Input:

```text
payload
explicit callsign-hash state/interface
```

Output:

```text
protocol message type
canonical decoded text
field metadata / structured information
payload/hash identity
```

### Slot result

The RX block ultimately returns multiple unique messages:

```text
RxBatch
    slot identity
    message count
    RxMessage[]
```

Conceptual per-message metadata:

```text
decoded/structured message
protocol message type
candidate score        diagnostic
frequency offset
time offset
SNR                     V2 baseline first; later algorithm may improve
LDPC/CRC diagnostics    optional/debug-facing
payload/hash identity
```

Exact public MiniFT8 types will be defined only after the remaining production V2 decode path and message/hash code are reviewed.

## 7. Explicit non-goals for the decoder

The FT8 decoder does not own:

```text
WAV parsing
MiniShell Audio
channel 0/1 ordinary-audio versus I/Q meaning
UTC slot selection
real-time sleeping/pacing
UI rendering/sorting
logical CQ/to-me application classification
AutoSeq policy
TX decisions
ADIF logging
radio/CAT control
```

Important nuance:

> The engine may later *use* local station identity as an explicit deep-search hint while still not *owning* reply-to-me application classification or QSO policy.

Those responsibilities stay at their existing boundaries.

## 8. Next RX-0B reading

Completed source reviews:

```text
decode_helper.cpp
production decode_monitor_results() responsibility review
```

Next review:

```text
monitor.h / monitor.c
    - explicit struct versus hidden static storage
    - FFT/workspace/waterfall ownership
    - block and OSR assumptions
```

Then:

```text
decode.h / decode.c
    - candidate representation
    - Costas scoring/search
    - likelihood/LDPC/CRC boundaries

message.h / message.c
    - payload/message boundary
    - message type + field metadata
    - callsign hash contract
```

Only after those reviews should RX-1 begin moving or cleaning implementation code.

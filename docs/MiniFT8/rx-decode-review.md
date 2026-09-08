# MiniFT8-V3 RX — `decode.h / decode.c` Review

## Purpose

This document continues RX-0B by reviewing MiniFT8-V2 `components/ft8_lib/ft8/decode.h` and `decode.c`.

The goal is to preserve the proven V2 FT8/FT4 decoding mathematics while cleaning policy, ownership, diagnostics, and extension points before code is moved into V3.

## Overall classification

Compared with `monitor.c`, the decode core is already structurally much cleaner.

```text
candidate search mathematics       KEEP
likelihood extraction              KEEP
likelihood normalization           KEEP
LDPC / CRC decode sequence         KEEP
candidate/result data model        KEEP internally, clean naming/status
search/decode policy constants     CLEAN / make explicit
input/output validation            CLEAN
unused historical experiments      DROP from V3 port
future deep-search extension       ADD later behind explicit engine context
```

The file has no hidden mutable decoder singleton, no heap allocation, and no platform/UI/MiniShell dependency. Candidate storage is caller-owned, and the main per-candidate work buffers are bounded automatic storage.

## Current V2 pipeline inside `decode.c`

```text
completed waterfall
    |
    v
ftx_find_candidates()
    - scan time/frequency positions
    - Costas sync scoring
    - keep top N candidates in caller array
    - return descending-score list
    |
    v
for each candidate
    |
    +-> FT4/FT8 likelihood extraction
    +-> likelihood normalization
    +-> belief-propagation LDPC decode
    +-> parity-error check
    +-> pack decoded bits
    +-> CRC extract/calculate/validate
    +-> FT4 de-whitening when applicable
    `-> protocol payload
```

Message unpacking is not part of this file. `ftx_decode_candidate()` stops at the encoded FT8/FT4 payload represented by `ftx_message_t`.

That is a useful boundary to preserve.

## `ftx_waterfall_t`

`decode.h` defines the completed waterfall consumed by the decoder:

```text
max_blocks
num_blocks
num_bins
time_osr
freq_osr
mag
block_stride
protocol
```

The decoder treats the waterfall as read-only input. This is correct ownership.

In V3, raw `ftx_waterfall_t` should remain internal to `ft8_engine`; application code should not depend on it.

## `ftx_candidate_t`

A candidate is a compact location/score descriptor:

```text
score
time_offset
freq_offset
time_sub
freq_sub
```

This is a good internal representation.

Important semantic rule:

> `score` is the Costas/synchronization candidate score. It is not SNR.

Candidate coordinates are intrinsic decoder metadata and may later be translated into frequency/time estimates for the engine result.

## Candidate search

`ftx_find_candidates()` is pure over:

```text
waterfall
candidate capacity
minimum score
caller-supplied candidate array
```

It scans:

```text
time subdivisions
frequency subdivisions
time_offset = -10 .. 19
all valid frequency offsets
```

and retains the strongest N candidates using a min-heap, finally sorting them into descending score order.

### Good properties

- no allocation;
- candidate storage is caller-owned;
- capacity and minimum score are already caller parameters;
- FT4 and FT8 sync scorers are selected by protocol;
- no station/QSO/UI state.

### Policy that should become explicit

The `-10 .. 19` time-search window is currently embedded in the algorithm. During initial V3 structural cleanup it should remain unchanged, but it should eventually be represented as decoder-search policy rather than an unexplained permanent constant.

Production V2 baseline remains:

```text
candidate capacity = 50
minimum score      = 5
```

Those values belong to an explicit V3 decode profile/policy, not to FT8 protocol constants.

### Candidate-order golden rule

Candidate results are useful golden diagnostics, but exact ordering among equal-score candidates should not become a brittle external contract. The hard regression target is the same candidate set/locations/scores for structurally equivalent code, with decoded payload behavior as the primary invariant.

## Likelihood extraction

FT4 and FT8 currently have separate internal routines:

```text
ft4_extract_likelihood()
ft8_extract_likelihood()
```

They convert waterfall magnitudes at a candidate location into the 174 soft values consumed by LDPC decoding.

This is a very important internal boundary for future algorithm work:

```text
candidate + waterfall
        |
        v
174 likelihood values
        |
        v
FEC decoder
```

The current mathematics should be preserved exactly during RX-1 cleanup.

Magic symbol-offset values in these routines should eventually be replaced by named protocol constants, but only as a structural readability change verified by golden tests.

## Likelihood normalization

`ftx_normalize_logl()` computes variance and scales the 174 likelihood values using the existing experimentally chosen coefficient.

This is algorithm behavior and should be preserved initially.

One robustness question is recorded for later: an all-equal likelihood vector can produce zero variance. V3 should eventually define behavior for that edge case explicitly, but this must be isolated from the first structural port so a robustness fix is not confused with an algorithm change.

## LDPC boundary

`ftx_decode_candidate()` calls:

```text
bp_decode(log174, max_iterations, plain174, &ldpc_errors)
```

The production V2 baseline uses:

```text
max_iterations = 25
```

The active `bp_decode()` implementation reports the minimum number of parity-check errors, where:

```text
0 = successful LDPC codeword
>0 = failed to converge to a valid codeword
```

The current comment in `ldpc.h` saying `ok == 87 means success` is stale/inconsistent with the active implementation and should not be carried into the cleaned V3 interface.

LDPC remains a pure FT8 math dependency inside `ft8_engine`.

## CRC/payload boundary

After successful LDPC parity checks, the decoder:

```text
packs the decoded information bits
    -> extracts transmitted CRC
    -> calculates CRC
    -> compares CRCs
    -> returns failure on mismatch
    -> produces 77-bit protocol payload
```

FT4 then applies its protocol XOR/de-whitening step before exposing the payload.

This is a clean boundary:

```text
candidate decode success
    -> validated protocol payload
```

Message interpretation belongs to the next `message.h / message.c` layer.

## Payload identity / hash

V2 stores the calculated CRC in `message->hash` and later performs payload dedupe using both hash and payload comparison.

The CRC therefore acts only as a fast key; equality still requires payload comparison. That is collision-safe for V2's dedupe logic and should be preserved during initial cleanup.

A stronger payload identity can be considered later if useful, but it is not required for RX-1.

## Decode status cleanup

Current `ftx_decode_status_t` contains:

```text
freq
time
ldpc_errors
crc_extracted
crc_calculated
```

But `decode.c` only writes the LDPC and CRC fields. `freq` and `time` are not populated there; production V2 derives frequency/time separately from candidate/monitor geometry.

V3 should not keep misleading dead fields in the candidate-decode status.

Conceptually separate:

```text
candidate-decode diagnostics
    LDPC parity errors
    extracted CRC
    calculated CRC
    decode failure stage

candidate location/measurement
    candidate score
    time offset
    frequency offset
    SNR estimate
```

Also, a V3 candidate decode result should be initialized by the callee and expose an explicit outcome such as:

```text
OK
LDPC_FAIL
CRC_FAIL
INVALID_INPUT
```

rather than relying only on a boolean plus partially written status memory.

Exact C types are deferred until RX-1 implementation design.

## Input validation

The V2 functions assume valid pointers, dimensions, candidate capacity, and candidate coordinates.

One concrete example: `ftx_find_candidates()` with candidate capacity zero can enter logic that references `heap[0]`.

V3 should add inexpensive boundary validation around the cleaned internal API. This is structural robustness, not DSP policy.

Validation must not add platform dependencies or dynamic allocation.

## Historical/dead code to omit from V3

Two pieces are currently unused:

```text
db_power_sum[]
ft8_decode_multi_symbols()
```

There is also disabled alternative likelihood code behind compile-time branches/comments.

These are useful history in the V2 repository, but they should not be copied into the initial clean V3 decoder. Removing dead alternatives reduces the amount of code that must be understood and maintained.

If a future experiment needs them, Git history/V2 remains the reference.

## Deep-search extension point

The earlier RX documents allowed an optional local callsign as decoder search context. Reviewing `decode.c` sharpens that rule.

A future reply-to-me deep-search algorithm may use station identity in more than one place:

```text
A. candidate discovery/ranking

or

B. prior-assisted likelihood / LDPC decoding

or

C. an additional decode pass combining both
```

Therefore V3 must **not** hard-wire the station hint specifically into `ftx_find_candidates()`.

Preferred conceptual shape:

```text
Ft8DecodePass
    waterfall
    decode policy
    optional Ft8SearchContext
        deep_search_enabled
        local_callsign
        future explicit decoder hints
```

Normal pass:

```text
waterfall
    -> normal candidate finder
    -> normal likelihood extraction
    -> normal LDPC/CRC decode
```

Possible future deep pass:

```text
same waterfall
    -> normal or expanded candidate source
    -> likelihood extraction
    -> apply allowed a-priori constraints/hints
    -> constrained/prior-assisted decode
    -> CRC validation
```

The engine may use the local callsign to improve recovery, but it still does not know:

```text
whether to reply
current AutoSeq state
TX stage
IgnoreList
UI priority
which station the operator wants to work
```

Deep search is decoder strategy, not QSO policy.

## Recommended V3 internal decomposition

Do not necessarily make each line a public source module, but preserve these conceptual/test boundaries:

```text
completed waterfall
    |
    v
candidate finder
    |
    v
candidate descriptor
    |
    v
likelihood extractor
    |
    v
174 soft bits
    |
    v
likelihood normalization
    |
    v
FEC/LDPC decoder
    |
    v
CRC/payload validator
    |
    v
validated protocol payload
```

Then the next layer:

```text
validated payload
    -> message codec
    -> protocol message type + structured fields
```

## Golden tests recommended before/while RX-1

### Candidate-search golden

```text
fixed waterfall
    -> candidate finder
    -> candidate locations + scores
```

This isolates Costas/sync search from LDPC and message parsing.

### Candidate-decode golden

```text
fixed waterfall + fixed candidate
    -> likelihood / LDPC / CRC
    -> exact validated payload
```

Record diagnostic LDPC/CRC values as useful evidence.

### Slot-level decoder golden

```text
fixed waterfall
    -> production V2 policy
    -> same unique protocol payloads/messages
```

This remains the strongest hard invariant.

## RX-0B status after this review

Completed:

```text
decode_helper.cpp
production decode_monitor_results()
monitor.h / monitor.c
decode.h / decode.c
```

Next review:

```text
message.h / message.c
```

That final RX-0B source review should settle:

- protocol message type and structured field ownership;
- callsign hash-store interface/lifecycle;
- Field Day, DXpedition, nonstandard-call, and free-text boundaries;
- what `ft8_engine` returns versus what `rx_result_builder` derives.

Only after that should RX-1 begin moving/cleaning FT8 implementation code.

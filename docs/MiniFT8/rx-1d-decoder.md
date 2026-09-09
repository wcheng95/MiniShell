# MiniFT8-V3 RX-1D — Candidate Search, LDPC, and CRC

Status: **COMPLETE**

RX-1D migrates the FT8 candidate-search and payload-validation path behind the MiniFT8 `ft8_engine` boundary while preserving the pinned MiniFT8-V2 decode policy and exact RX-1A payload behavior.

RX-1D is still a structural cleanup stage. It does not add message unpacking, callsign-hash state, deep search, SNR changes, candidate-ranking changes, AutoSeq policy, or UI behavior.

## 1. Scope

Implemented production boundary:

```text
Ft8WaterfallView
      |
      v
candidate search
      |
      v
likelihood extraction
      |
      v
belief-propagation LDPC
      |
      v
CRC-14 validation
      |
      v
Ft8DecodedPayload
```

RX-1D stops at a validated 10-byte FT8 payload plus factual decoder diagnostics.

The following deliberately remain later stages:

```text
callsign hash store        RX-1E
protocol message unpack    RX-1F
canonical message text     RX-1F
station-aware RX result    later RX assembly
AutoSeq / reply policy     separate milestone
```

## 2. Pinned V2 decode policy preserved

The structural-cleanup baseline remains:

```text
candidate capacity       50
minimum sync score        5
maximum LDPC iterations  25
time search              -10 .. +19 blocks
FT8 tones                  8
FT8 data symbols          58
```

The candidate search still uses the V2 Costas pattern:

```text
3 1 4 0 6 5 2
```

and the V2 Gray map:

```text
0 1 3 2 5 6 4 7
```

No attempt was made to improve search sensitivity, candidate ranking, OSR, likelihood extraction, LDPC convergence, or SNR.

## 3. Public-to-engine types

Implemented in:

```text
apps/ft8/src/ft8_engine/ft8_decoder.h
apps/ft8/src/ft8_engine/ft8_decoder.c
```

The cleaned candidate is MiniFT8-owned:

```c
typedef struct {
    int16_t score;
    int16_t time_offset;
    int16_t freq_offset;
    uint8_t time_sub;
    uint8_t freq_sub;
} Ft8Candidate;
```

The decoder output is deliberately payload-level rather than message-level:

```c
typedef struct {
    Ft8Candidate candidate;
    int ldpc_errors;
    uint16_t crc_extracted;
    uint16_t crc_calculated;
    uint8_t payload[10];
} Ft8DecodedPayload;
```

This keeps RX-1D independent of V2 `ftx_message_t`, text rendering, and callsign-hash ownership.

## 4. Status boundary

RX-1D returns explicit private MiniFT8-domain statuses:

```text
FT8_DECODER_OK
FT8_DECODER_ERR_INVALID
FT8_DECODER_ERR_LDPC
FT8_DECODER_ERR_CRC
```

A failed LDPC decode and a valid LDPC codeword with a bad CRC are distinct outcomes.

These are not MiniShell API status values.

## 5. Candidate search cleanup

The V2 search mathematics and heap policy are preserved:

```text
scan time_sub
scan freq_sub
scan time_offset -10..19
scan all possible eight-tone frequency starts
score FT8 Costas neighborhoods
prune below min score
retain best N in min-heap
sort retained candidates strongest first
```

### Safe waterfall indexing

V2 first formed a candidate-relative pointer which could temporarily point before the waterfall array for negative `time_offset`, then relied on later symbol offsets to return into valid storage.

RX-1D instead resolves each requested symbol to an absolute block first:

```text
candidate time_offset + symbol index
        |
        +-- outside captured blocks -> unavailable symbol
        `-- valid block             -> calculate in-array offset
```

This removes pointer-before-array behavior without changing the intended search semantics.

The pinned RX-1A payload regression passes with this safer indexing.

## 6. Likelihood extraction

RX-1D preserves the V2 FT8 max-log likelihood path.

For each of 58 data symbols:

```text
first 29 data symbols   skip first 7 Costas symbols
second 29 data symbols  skip 14 accumulated Costas symbols
```

Waterfall bytes retain the V2 interpretation:

```c
dB = byte * 0.5f - 120.0f;
```

The same three max-log bit likelihoods are generated from the Gray-mapped eight tones.

The 174 likelihoods are normalized using the same V2 float operation order. As RX-1C demonstrated, apparently equivalent DSP algebra is not casually reassociated during structural cleanup.

## 7. LDPC ownership and cleanup

Implemented in:

```text
apps/ft8/src/ft8_engine/ft8_ldpc.h
apps/ft8/src/ft8_engine/ft8_ldpc.c
```

RX-1D keeps the V2 belief-propagation decoder, including:

```text
174-bit codeword
83 parity checks
3 checks per codeword bit
V2 fast_tanh approximation
V2 fast_atanh approximation
same hard-decision rule
same all-zero stop rule
same minimum parity-error reporting
```

### One canonical parity topology

V2 stores both directions of the LDPC graph:

```text
Nm: parity-check row -> codeword bits
Mn: codeword bit -> three parity-check rows
```

`Mn` is completely derivable from `Nm`. RX-1D therefore keeps one canonical parity-check table and reconstructs the three reverse references in ascending parity-row order when decoding.

This removes duplicated topology data while preserving the ordering consumed by the V2 belief-propagation loops.

The exact RX-1A payload golden passes, providing the behavioral proof for this structural simplification.

This is not an LDPC algorithm change.

## 8. CRC boundary

Implemented in:

```text
apps/ft8/src/ft8_engine/ft8_crc.h
apps/ft8/src/ft8_engine/ft8_crc.c
```

The private CRC helper preserves the V2 FT8 CRC-14 behavior:

```text
width       14
polynomial  0x2757
input       77 payload bits + 5 zero-extension bits
CRC bits    extracted from the 91-bit LDPC information field
```

Only payloads passing both LDPC parity and CRC validation return `FT8_DECODER_OK`.

## 9. Deliberately absent message/hash state

V2 `ftx_decode_candidate()` wrote into `ftx_message_t`, including a CRC-derived message hash.

RX-1D intentionally does not do that.

The output identity at this boundary is simply the exact validated payload:

```text
uint8_t payload[10]
```

Rules:

- CRC is validation metadata, not collision-free message identity;
- exact payload bytes are message identity for later dedupe/codec work;
- callsign hash resolution is not part of candidate decoding;
- no global/persistent hash table exists in RX-1D;
- no canonical text is generated in RX-1D.

## 10. Tests

### Decoder unit test

```text
tests/ft8_decoder_rx1d_test.c
```

Covers:

```text
locked 50 / 5 / 25 policy constants
candidate-capacity enforcement
minimum-score pruning
invalid waterfall rejection
invalid decoder arguments
explicit LDPC failure
```

The normal Linux CI suite runs this test without external reference files.

### Pinned V2 payload regression

```text
tests/ft8_decoder_rx1d_reference.c
.github/workflows/rx1d-reference.yml
```

CI checks out exactly:

```text
wcheng95/Mini-FT8
5bd3ef98f72388a850bebad04bd7300b90edb63c
```

only to obtain the frozen 6 kHz RX-1A WAV.

The test first re-verifies the RX-1C prerequisite:

```text
blocks        85
active bytes  73610
waterfall     18BE1E838FD9C6AF
```

Then it runs the migrated RX-1D path with:

```text
capacity       50
minimum score   5
LDPC iterations 25
```

After exact-payload deduplication it must produce exactly one unique valid payload:

```text
000000206016500A1988
```

This is the RX-1A frozen `CQ W1XYZ FN42` protocol payload. Text decoding itself remains outside RX-1D.

Candidate score/order are intentionally not elevated to permanent golden contracts; RX-1A defined them as diagnostic behavior rather than message identity.

## 11. CI result

RX-1D passed:

```text
ft8_decoder_rx1d_unit       PASS
ft8_decoder_rx1d_reference  PASS
Linux full suite            PASS
RX-1C pinned regression     PASS
```

The first RX-1D CI attempt stopped at a strict C11/pedantic test-harness qualifier warning before executing the golden. The harness declaration was corrected; no decoder algorithm change was involved.

## 12. RX-1D exit criteria

```text
[done] FT8-only candidate-search owner behind ft8_engine
[done] V2 50 / 5 / 25 decode policy preserved
[done] V2 Costas/sync scoring preserved
[done] safer in-array waterfall addressing
[done] V2 FT8 likelihood extraction preserved
[done] V2 BP-LDPC behavior preserved
[done] duplicate LDPC reverse topology removed deterministically
[done] V2 CRC-14 behavior preserved
[done] exact validated payload returned without message/hash coupling
[done] frozen RX-1A payload reproduced exactly
[done] no MiniShell/platform/UI/AutoSeq/TX dependency
[done] no message codec or persistent callsign-hash migration mixed into RX-1D
```

## 13. Next stage

Next active stage:

**RX-1E — introduce an explicit per-engine `Ft8HashStore` for hashed-callsign knowledge across slots.**

RX-1E should establish ownership, lookup/save/aging semantics, and multi-instance independence before the typed protocol message codec is migrated in RX-1F.

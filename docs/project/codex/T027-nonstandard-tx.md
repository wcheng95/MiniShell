# T027 — Non-standard / hashed FT8 TX support

Status: READY

## Architect intent

Revisit the T021 TX encoder limitation and add support for real non-standard callsigns, using the pinned MiniFT8-V2 encoder as the behavioral reference.

The live trigger is:

```text
DX = W1AW/9
```

V3 can decode/select that station and place it in AutoSeq, but physical TX never starts because T021 deliberately rejects non-standard/hash TX. This task closes that gap without redesigning AutoSeq, CAT, scheduling, or the physical executor.

## Objective

Extend the existing pure V3 FT8 TX encoder/codec so AutoSeq-generated QSO replies to non-standard callsigns encode correctly and produce a valid immutable `Ft8TxPlan`.

Primary required behavior:

```text
received/selected: CQ W1AW/9
next semantic TX1: W1AW/9 AG6AQ CM97
                     |
                     +-- encode W1AW/9 as the V2-compatible 22-bit hash
                         inside the normal STANDARD type-1 message

result:
    Ft8TxPlan valid
    canonical_text = W1AW/9 AG6AQ CM97
    physical mocked-QMX path can key normally
```

This is a follow-up to T021. Do not rewrite T021 history.

## Current context

T021 intentionally states:

```text
Nonstandard/hashed-call TX remains unsupported.
```

Current V3 already has:

- RX decode for `FT8_PROTOCOL_NONSTD_CALL` (i3=4);
- the V2-compatible callsign hash mathematics in `ft8_protocol_callsign_hash22()`;
- `Ft8HashStore` with 22/12/10-bit lookup;
- AutoSeq storage of the normalized full DX call;
- physical Linux/QMX TX using the immutable T021 `Ft8TxPlan`.

Current TX failure boundary:

```text
AutoSeqTxIntent
 -> tx_encoder project()
 -> FT8_PROTOCOL_STANDARD
 -> pack_call()
 -> W1AW/9 rejected as unsupported
 -> ft8_tx_encode() fails
 -> app_controller: "ft8: TX plan/identity encoding failed"
 -> no RF TX
```

The user has observed the station enter the TX queue but no transmission occurs.

## Source of truth

Read before editing:

```text
AGENTS.md
docs/MiniFT8/architecture.md
docs/MiniFT8/development.md
docs/project/progress.md

docs/project/codex/T021-ft8-tx-encoder.md
docs/project/codex/T026-rr73-signoff.md

apps/ft8/src/tx_encoder/tx_encoder.[ch]
apps/ft8/src/ft8_engine/ft8_message_codec.[ch]
apps/ft8/src/ft8_engine/ft8_hash_store.[ch]
apps/ft8/src/auto_seq/auto_seq_tx_intent.[ch]
apps/ft8/src/rx_result_builder/rx_result_builder.c
apps/ft8/src/app_controller/app_controller_tx_physical.c

tests/ft8_tx_encoder_test.c
tests/ft8_physical_tx_test.c
tests/ft8_tx_vectors/
```

Pinned V2 reference:

```text
repository: wcheng95/Mini-FT8
commit:     491e757ae6b1e4cfd2b9a6ba10f48b35643849e0

components/ft8_lib/ft8/message.c
components/ft8_lib/ft8/message.h
main/autoseq.cpp
tests/tx_e2e/
```

Relevant V2 behavior:

1. `pack28()` first tries a standard basecall.
2. If that fails and the call is a valid 3..11-character FT8 non-standard call,
   it computes the 22-bit callsign hash and packs `NTOKENS + n22`.
3. Therefore directed QSO text such as:

   ```text
   W1AW/9 AG6AQ CM97
   W1AW/9 AG6AQ -12
   W1AW/9 AG6AQ R-08
   W1AW/9 AG6AQ RR73
   W1AW/9 AG6AQ 73
   ```

   remains a normal STANDARD FT8 message. The non-standard callsign occupies a
   hashed 28-bit callsign field; the grid/report/terminal field remains available.
4. V2 type-4 (`i3=4`) carries one full 58-bit non-standard call plus one 12-bit
   hashed call. In particular, a CQ from a non-standard station uses type-4:

   ```text
   CQ PJ4/KA1ABC
   ```

   and cannot also carry a grid.
5. V2 TX logging uses the intended TX text, not a re-decoded `<...>` placeholder.

Do not vendor V2. Port only the required pure algorithms/semantics.

## Architectural constraints

1. Keep the T021 encoder pure C, deterministic, platform-independent, and no-heap.
2. No MiniShell API, CAT, Serial, Audio, filesystem, UI, clock, or radio dependency may enter the codec/encoder.
3. AutoSeq remains semantic policy. It continues to carry the full normalized callsign, e.g. `W1AW/9`.
4. Do not add persistent TX hash state merely to encode a call. Hash calculation is deterministic from the full callsign.
5. RX `Ft8HashStore` ownership/lifecycle remains unchanged.
6. No public MiniShell API change.
7. Existing standard-call payload/tone vectors must remain byte-for-byte unchanged.
8. Do not silently fall back to free text for a QSO that requires hash/non-standard encoding.
9. Do not infer message meaning from display text; preserve the typed protocol boundary.
10. T026's temporary RR73 workaround is out of scope.

## Required protocol behavior

### A. Directed QSO — V2 22-bit hash fallback

For STANDARD type-1/type-2 callsign packing, preserve V2 `pack28()` behavior:

- first attempt normal basecall encoding;
- preserve standard `/P` and `/R` handling exactly as today;
- if normal basecall encoding fails, accept a non-standard callsign only when:
  - length is 3..11;
  - every character belongs to the FT8 callsign alphabet used by V2 for hashing
    (uppercase letters, digits, and `/`; normalized input is already uppercase);
- compute the existing 22-bit FT8 callsign hash;
- pack it as `FT8_NTOKENS + hash22`;
- no store mutation is required.

This must make all existing AutoSeq QSO stages encode:

```text
TX1  W1AW/9 AG6AQ CM97
TX2  W1AW/9 AG6AQ -12
TX3  W1AW/9 AG6AQ R-08
TX4  W1AW/9 AG6AQ RR73
TX5  W1AW/9 AG6AQ 73
```

The first five are STANDARD messages, not type-4 substitutions.

The same rule should work if the non-standard callsign is the other station field, and should naturally apply to ARRL Field Day c28 packing where the current typed encoder uses the same callsign representation.

### B. Post-encode validation must allow legitimate hashes

Current T021 re-decodes the generated payload and rejects `has_unresolved_hash`.

That is no longer a valid rejection criterion for a legitimate TX payload containing a freshly computed callsign hash.

A valid hash-bearing TX must not fail merely because a decode performed without the RX hash store renders a callsign as:

```text
<...>
```

Preserve a strong structural validation step, but make it hash-aware. Acceptable approaches include a temporary local hash store seeded only for validation, or equivalent deterministic typed checks.

Do not weaken validation for malformed payloads.

### C. TX plan canonical text

For a directed hash-bearing QSO, `Ft8TxPlan.canonical_text` must remain the full intended TX text known by the transmitter:

```text
W1AW/9 AG6AQ CM97
```

not:

```text
<...> AG6AQ CM97
```

and not:

```text
<W1AW/9> AG6AQ CM97
```

This preserves V2 TX-log semantics and makes the current RT T record useful.

Existing standard-call canonical text behavior, including normalized report spelling, must remain unchanged.

### D. Type-4 encoder support

Extend the typed V3 protocol encoder to support `FT8_PROTOCOL_NONSTD_CALL` using the pinned V2 layout:

```text
12-bit hash
58-bit full callsign
iflip
nrpt[2]
icq
i3 = 4
```

Support the existing `Ft8ProtocolNonstandard` representation:

- plain non-standard CQ;
- directed type-4 messages;
- terminals NONE / RRR / RR73 / 73.

Use V2-compatible 12-bit hash derivation:

```text
hash12 = hash22 >> 10
```

and V2-compatible 58-bit base-38 full-call packing.

### E. AutoSeq plain CQ with a non-standard local call

When the local station callsign itself cannot be represented as a standard basecall, a plain:

```text
CQ <MYCALL> <GRID>
```

cannot retain the grid in FT8 type-4.

For `AUTO_SEQ_CQ`, encode the V2-compatible actual on-air form:

```text
CQ <MYCALL>
```

using `FT8_PROTOCOL_NONSTD_CALL`, and set plan canonical text to that actual transmitted form.

Do not pretend the grid was transmitted.

For modified CQ intents whose semantics cannot be represented by type-4:

```text
CQ SOTA
CQ POTA
CQ QRP
CQ FD
```

with a non-standard local callsign, return `FT8_TX_ENCODE_UNSUPPORTED`.

Do **not** silently drop the modifier and transmit plain CQ. This is an intentional V3 safety rule even though V2's generic fallback can lose CQ modifier/grid information.

Free-text CQ behavior is unchanged.

## Hash/store semantics

The TX encoder computes hashes from the full known callsign. It does not depend on the RX hash store to create the payload.

For tests, distinguish:

```text
TX knowledge:
    full string W1AW/9 -> deterministic hash -> valid payload

RX decode with empty store:
    hashed field -> <...>, has_unresolved_hash=true

RX decode with seeded store:
    hashed field -> <W1AW/9>, has_unresolved_hash=false
```

Both decodes may describe the same valid payload.

Do not turn unresolved RX display state into an encoder failure.

## Exact live regression

Add a production-path regression for the reported behavior.

Use station:

```text
callsign=AG6AQ
grid=CM97
```

Create/decode a pinned-V2-compatible non-standard CQ for:

```text
CQ W1AW/9
```

Pass it through the real V3 protocol decode -> RxResultBuilder -> user/select action -> AutoSeq path.

Prove:

1. the full DX call reaches AutoSeq as `W1AW/9`;
2. next intent is TX1;
3. `ft8_tx_encode()` returns OK;
4. plan text is exactly:
   ```text
   W1AW/9 AG6AQ CM97
   ```
5. protocol type for that TX1 payload is STANDARD;
6. the physical mocked-QMX executor starts TX instead of reporting
   `TX plan/identity encoding failed`;
7. the RT T line contains the same full intended text;
8. offset resolution / CAT / absolute scheduling remain unchanged.

Do not inject a pre-classified AutoSeq event as the only regression; exercise the decoded non-standard CQ boundary that produced the real failure.

## V2 fixed vectors

Extend the checked-in V2-derived vector set. Test runtime must not require the V2 repository.

At minimum lock exact payload + 79-tone vectors for:

```text
W1AW/9 AG6AQ CM97
W1AW/9 AG6AQ -12
W1AW/9 AG6AQ R-08
W1AW/9 AG6AQ RR73
W1AW/9 AG6AQ 73
CQ W1AW/9
```

Also include at least one directed case with the non-standard call in the opposite callsign field.

Generate the vectors from the pinned V2 commit using the existing independent T021 vector-generation method. Record provenance and updated SHA-256 in the vector README/task notes.

Required type checks:

- the five directed W1AW/9 QSO vectors are `FT8_PROTOCOL_STANDARD`;
- plain `CQ W1AW/9` is `FT8_PROTOCOL_NONSTD_CALL` / i3=4.

## Negative tests

Prove rejection/cleared plan for:

- calls shorter than 3 when treated as non-standard;
- calls longer than 11;
- invalid hash alphabet characters;
- non-standard local call + CQ POTA/SOTA/QRP/FD;
- malformed type-4 terminal;
- any path that would otherwise silently fall back to free text.

Existing invalid/missing-field tests remain green.

## Non-goals

Do not implement or change:

- AutoSeq state transitions/retry policy;
- RR73 classification/permanent T026 redesign;
- QMX CAT syntax;
- Linux QMX endpoint discovery;
- slot timing or late-entry behavior;
- TX offset policy;
- RxTxLog format;
- UI layout;
- station/config file format;
- FT4;
- DXpedition behavior;
- new persistent callsign database;
- public MiniShell APIs.

Do not add generalized arbitrary message parsing. This task extends the existing typed TX path.

## Acceptance criteria

- [ ] exact live `W1AW/9` queue -> TX failure reproduced before the fix;
- [ ] directed non-standard DX calls use V2-compatible 22-bit hash packing in STANDARD messages;
- [ ] TX1..TX5 for `W1AW/9` all encode;
- [ ] exact V2 payload/tone vectors match;
- [ ] plain non-standard CQ uses type-4 and exposes the full non-standard call;
- [ ] non-standard CQ plan text reflects the actual no-grid type-4 message;
- [ ] unsupported modified CQ is rejected rather than degraded;
- [ ] legitimate unresolved self-check hashes no longer invalidate TX;
- [ ] plan canonical text retains full intended directed callsign;
- [ ] physical mocked-QMX path keys for the reported `W1AW/9` case;
- [ ] RT T line contains `W1AW/9 AG6AQ CM97`;
- [ ] all existing standard vectors remain unchanged;
- [ ] T026 regressions remain green;
- [ ] no heap / platform dependency added;
- [ ] Linux full CTest passes;
- [ ] portable unit suite passes;
- [ ] architecture checks pass;
- [ ] focused sanitizers pass;
- [ ] real ADV build passes;
- [ ] `git diff --check` passes;
- [ ] no unrelated cleanup.

## Automated tests

Run at minimum:

```bash
git status --short

cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure

cmake -S tests/unit -B /tmp/T027-build-unit
cmake --build /tmp/T027-build-unit -j"$(nproc)"
ctest --test-dir /tmp/T027-build-unit --output-on-failure

PYTHONDONTWRITEBYTECODE=1 python3 tests/app_dependency_boundary.py . ft8
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_platform_boundary.py . ft8
PYTHONDONTWRITEBYTECODE=1 python3 tests/ft8_platform_boundary.py .
PYTHONDONTWRITEBYTECODE=1 python3 tests/architecture_rules.py .

# Focused ASan/UBSan on changed pure/physical regression targets.

source ~/projects/esp-idf/export.sh
idf.py -C platform/adv build

git diff --check
```

No GitHub Actions wait.

## Manual / hardware validation

No RF transmission is required for Codex.

After supervisor review, the architect may repeat the original Linux/QMX scenario when a non-standard station is available. The useful evidence is:

```text
R ... CQ W1AW/9
T ... W1AW/9 AG6AQ CM97
```

with actual QMX keying and no:

```text
ft8: TX plan/identity encoding failed
```

Because this task changes only pure encoding plus regression coverage of the already-validated physical executor, fixed V2 vectors and the mocked physical path are the software acceptance gate.

## Branch workflow

Use:

```text
codex/T027-nonstandard-tx
```

Codex:

1. read this task and T021;
2. reproduce the W1AW/9 failure before changing production code;
3. inspect the pinned V2 `pack28()`, `ftx_message_encode_nonstd()`, hash and `pack58()` implementations;
4. implement the smallest typed V3 extension;
5. extend fixed vectors and end-to-end regression;
6. run all required gates;
7. set Status to REVIEW;
8. record exact failing-before/fixed-after evidence and vector provenance;
9. commit/push one reviewable implementation commit;
10. return exact SHA;
11. no PR;
12. no Actions wait.

## Codex implementation notes

### Implementation summary

### Files changed

### Behavior/invariants preserved

### V2 vector provenance

### Failing-before / passing-after evidence

### Local tests run

### Hardware/manual validation still required

### Known limitations / risks

### Commit

## Supervisor review

Review the exact task-head .. implementation diff, not only these notes.

## Architect test result


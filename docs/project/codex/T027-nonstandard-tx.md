# T027 — Non-standard / hashed FT8 TX support

Status: COMPLETE

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

- [x] exact live `W1AW/9` queue -> TX failure reproduced before the fix;
- [x] directed non-standard DX calls use V2-compatible 22-bit hash packing in STANDARD messages;
- [x] TX1..TX5 for `W1AW/9` all encode;
- [x] exact V2 payload/tone vectors match;
- [x] plain non-standard CQ uses type-4 and exposes the full non-standard call;
- [x] non-standard CQ plan text reflects the actual no-grid type-4 message;
- [x] unsupported modified CQ is rejected rather than degraded;
- [x] legitimate unresolved self-check hashes no longer invalidate TX;
- [x] plan canonical text retains full intended directed callsign;
- [x] physical mocked-QMX path keys for the reported `W1AW/9` case;
- [x] RT T line contains `W1AW/9 AG6AQ CM97`;
- [x] all existing standard vectors remain unchanged;
- [x] T026 regressions remain green;
- [x] no heap / platform dependency added;
- [x] Linux full CTest passes;
- [x] portable unit suite passes;
- [x] architecture checks pass;
- [x] focused sanitizers pass;
- [x] real ADV build passes;
- [x] `git diff --check` passes;
- [x] no unrelated cleanup.

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

Added deterministic 22-bit hash fallback to STANDARD/Field Day callsign packing,
typed type-4 packing, and plain non-standard local CQ projection without a grid.
Modified non-standard CQ remains unsupported. Post-encode validation decodes,
restores the known typed calls, and repacks for exact payload equality; canonical
text keeps full intended calls and decoder-normalized report/exchange spelling.
No RX hash store is needed or mutated by TX.

One necessary additional codec correction: type-4 CQ previously tried to resolve
its unused destination hash and marked the CQ unresolved. This prevented the
required empty-store decode/select regression from reaching AutoSeq. CQ now skips
that irrelevant lookup; directed RX hash lookup and store ownership are unchanged.
No architectural or AutoSeq policy change was needed.

### Files changed

- `apps/ft8/src/ft8_engine/ft8_message_codec.[ch]`: hash fallback, type-4 packing,
  and unused CQ hash correction.
- `apps/ft8/src/tx_encoder/tx_encoder.c`: CQ projection, hash-aware structural
  validation, full intended TX text.
- `tests/ft8_tx_encoder_test.c`: fixed vectors, empty/seeded RX store semantics,
  type checks, type-4 terminals, hashed FD and rejection/cleared-plan coverage.
- `tests/ft8_physical_tx_test.c`: real decoded CQ -> builder -> selection ->
  AutoSeq -> encoder -> mocked QMX -> RT regression.
- `tests/ft8_tx_vectors/{generate.py,vectors.h,README.md}`: independent oracle
  extension and provenance.
- This task packet: implementation and validation handoff.

### Behavior/invariants preserved

Original 25 payload/tone vectors unchanged; standard /P and /R, report spelling,
free text and T026 regressions pass. No heap, platform dependency, persistent TX
state, public API, config/UI/log format, CAT, offset policy, scheduling or AutoSeq
state transition change. Type-4 full typed calls use V2's default iflip=0 (source
full and destination hashed); no bracketed display-text input parser was added.

Old negative fixtures were narrowed because `INVALID`, `TEST`, `PJ4/K9XYZ`,
`A1BCDE`, and `ABC1DEF` now legitimately hash. The physical failure fixture uses
`INVALID!`; encoder rejection fixtures cover short/long/invalid-alphabet calls.

### V2 vector provenance

Oracle sources extracted with `git show` from local `/home/wei/projects/Mini-FT8`
at `491e757ae6b1e4cfd2b9a6ba10f48b35643849e0`. Inspected pinned pack28,
encode_nonstd, pack58 and hash behavior. No V2 source is vendored or required at
test runtime. 36 exact payload/79-tone vectors include the original 25, seven
required non-standard QSO/CQ cases and four typed directed type-4 terminal cases.

```bash
PYTHONDONTWRITEBYTECODE=1 python3 tests/ft8_tx_vectors/generate.py /home/wei/projects/Mini-FT8 > tests/ft8_tx_vectors/vectors.h
sha256sum tests/ft8_tx_vectors/vectors.h
# de9782c88f12e42ecfddb1206e5cd5e6edbc189aa2e5bacfe84d33adc1db47cb
```

Unmodified V2 debug formatting emits the existing LP64 `%llx` warnings.

### Failing-before / passing-after evidence

Before any production change, the regression failed at `auto_seq_prepare_tx_intent`
because of the unused CQ destination hash described above. After correcting only
that RX flag, with TX packing still unchanged, it reached the reported failure:

```text
W1AW/9 regression: encode=-2 active=0 text=
assertion rc==FT8_TX_ENCODE_OK && app.tx.active failed (exit 134)
```

After TX changes:

```text
W1AW/9 regression: encode=0 active=1 text=W1AW/9 AG6AQ CM97
```

The regression starts from the pinned CQ payload, uses production protocol decode,
RxResultBuilder and SELECT_RX_MESSAGE, checks the full DX/TX1 intent and STANDARD
payload, keys mocked QMX, verifies `] W1AW/9 AG6AQ CM97 1500` in RT, and completes
all 79 absolute symbol intervals. Existing exact CAT/offset/timing tests pass.
No pre-classified AutoSeq event substitutes for the CQ boundary.

### Local tests run

```bash
git status --short
cmake -S . -B build-linux
cmake --build build-linux -j8
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure
# PASS 58/58
cmake -S tests/unit -B /tmp/T027-build-unit
cmake --build /tmp/T027-build-unit -j8
ctest --test-dir /tmp/T027-build-unit --output-on-failure
# PASS 15/15
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_dependency_boundary.py . ft8
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_platform_boundary.py . ft8
PYTHONDONTWRITEBYTECODE=1 python3 tests/ft8_platform_boundary.py .
PYTHONDONTWRITEBYTECODE=1 python3 tests/architecture_rules.py .
# All PASS
cmake -S . -B /tmp/T027-build-sanitize \
  -DCMAKE_C_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
  -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined'
cmake --build /tmp/T027-build-sanitize --target ft8_physical_tx_unit ft8_tx_encoder_unit -j8
ctest --test-dir /tmp/T027-build-sanitize --output-on-failure -R 'ft8_physical_tx|ft8_tx_encoder'
# PASS 2/2 outside sandbox (LeakSanitizer cannot operate under sandbox ptrace)
source ~/projects/esp-idf/export.sh
idf.py -C platform/adv build
# PASS: real ESP32-S3 build
 git diff --check
# PASS
```

The initial full run had an unrelated `linux_serial_unit` timeout assertion at
line 67; its isolated retry and subsequent complete suite passed. No serial code
or test was changed. Existing untracked Python cache directories were left alone.

### Hardware/manual validation still required

No RF or hardware transmission performed. After supervisor review, architect may
repeat Linux/QMX reception/selection of a non-standard station and verify actual
keying plus matching full-call R/T records. Software gate uses fixed independent
vectors and mocked physical execution as specified.

### Known limitations / risks

FT8 hash collision/empty receiver-store ambiguity is inherent; TX knows the full
call and retains it in its log. Modified non-standard local CQ intentionally
returns unsupported. No arbitrary message parser or FT4 support added. The CQ RX
flag correction is the only addition beyond TX packing/projection and tests.

### Commit

One implementation commit on `codex/T027-nonstandard-tx`, based on task head
`78a4eb5`. The commit containing these notes is the implementation reference;
exact SHA is returned in the handoff. No PR or Actions wait.

## Supervisor review

PASS on implementation commit `8ee2d32144af4aa167138dd71d8cf1151e6646e6`.

Reviewed the exact single implementation commit from task head
`78a4eb5df09f8ab49c910b412334dc5ee1a87f07`. No blocking finding.

Accepted protocol behavior:

```text
directed W1AW/9 QSO:
    AutoSeq full call W1AW/9
      -> STANDARD type 1/2 family
      -> V2-compatible 22-bit hash in c28
      -> grid/report/RR73/73 field remains available

plain CQ from non-standard local call:
    CQ W1AW/9
      -> NONSTD_CALL / i3=4
      -> full 58-bit W1AW/9
      -> no grid on air
```

The production diff is appropriately bounded:

- `ft8_message_codec.c` adds V2-compatible deterministic c28 hash fallback and
  typed i3=4 packing; no persistent TX hash state is introduced.
- Standard `/P` and `/R` handling remains ahead of hash fallback and existing
  standard vectors remain unchanged.
- The type-4 CQ decoder no longer treats its unused n12 bits as an unresolved
  destination hash. This is accepted as a necessary factual decoder correction:
  the CQ source is fully carried in n58 and n12 has no station identity to resolve.
- `tx_encoder.c` converts only plain non-standard CQ to type 4. Modified
  SOTA/POTA/QRP/FD CQ returns UNSUPPORTED rather than silently losing semantics.
- Hash-bearing STANDARD/ARRL-FD self-validation remains strict: decode, restore
  only the known transmitted calls, repack, and require exact payload equality.
- Directed-plan canonical text preserves the transmitter-known full call
  (`W1AW/9 AG6AQ CM97`) while RX with an empty store may correctly display
  `<...>`.
- No AutoSeq, CAT, scheduler, offset, logging-format, UI, config, MiniShell API,
  or platform ownership change is present.

Accepted V2 oracle evidence:

```text
36 exact payload + 79-tone vectors
original T021 vectors: 25, unchanged
new T027 vectors:      11
vector SHA-256:
de9782c88f12e42ecfddb1206e5cd5e6edbc189aa2e5bacfe84d33adc1db47cb
```

Required directed cases TX1..TX5, opposite hashed STANDARD field, plain
non-standard CQ, and typed type-4 NONE/RRR/RR73/73 all match the pinned
MiniFT8-V2 commit `491e757ae6b1e4cfd2b9a6ba10f48b35643849e0`.

Accepted live-path regression:

```text
CQ W1AW/9
 -> production protocol decode
 -> RxResultBuilder
 -> SELECT_RX_MESSAGE
 -> AutoSeq TX1
 -> Ft8TxPlan = W1AW/9 AG6AQ CM97
 -> STANDARD payload
 -> mocked QMX keys
 -> RT T line keeps full W1AW/9 text
 -> all 79 absolute symbols complete
```

The reported pre-fix TX-plan failure is therefore covered at the correct boundary;
the test does not substitute a pre-classified AutoSeq event.

Accepted local evidence:

```text
Linux CTest          58/58 PASS
portable units       15/15 PASS
focused ASan/UBSan   2/2 PASS
architecture checks  PASS
real ADV build       PASS
git diff --check     PASS
```

The one initial `linux_serial_unit` timeout is non-blocking: isolated retry and
the subsequent complete suite passed, and T027 does not touch Serial code.

T027 is TESTING. Optional architect hardware confirmation is to select a real
non-standard station on Linux/QMX and observe actual keying plus a full-call RT
record. No RF evidence is required to accept the software implementation.


## Architect test result

Architect accepts T027 as COMPLETE based on the reviewed software evidence and
pinned-V2 vector coverage. Opportunistic Linux/QMX RF confirmation is deferred
until a compound/non-standard callsign appears naturally on air; it is not a
completion gate.

When such a station is encountered, useful confirmation remains:

```text
R ... CQ <compound-call>
T ... <compound-call> AG6AQ CM97
```

with actual QMX keying and the full intended callsign preserved in the RT T line.
Any future hardware discrepancy should open a new bounded task rather than reopen
T027 automatically.


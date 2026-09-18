# T021 — FT8 TX encoder and immutable tone plan

Status: READY

## Architect intent

Proceed toward the first MiniFT8-V3 Linux/QMX QSO without performing a separate
T020 RF test.

T020 is software/review complete and provides QMX CAT TX primitives, but its real
RF validation is explicitly deferred to T022.

T021 must therefore be completely pure and deterministic:

```text
AutoSeqTxIntent
    -> FT8 TX text
    -> 77-bit payload
    -> CRC + LDPC
    -> 79 FT8 tone indices
    -> immutable Tx plan
```

No clock, CAT, Serial, Audio, platform API, or RF belongs in this task.

## Objective

Implement the missing MiniFT8-V3 FT8 transmit encoder so a semantic
`AutoSeqTxIntent` can be converted into an immutable 79-symbol FT8 plan suitable
for T022's slot scheduler.

The plan must preserve pinned MiniFT8-V2/FT8 behavior:

```text
symbol count       79
symbol period      160 ms
tone spacing       6.25 Hz
tone values        0..7
Costas pattern     3 1 4 0 6 5 2
sync positions     0..6, 36..42, 72..78
```

T021 stops before any radio call.

## Accepted baseline

```text
T017  ADV live QMX RX                               COMPLETE
T018  Linux bare ft8 live RX                        COMPLETE
T019  Linux Serial/CDC + receive-safe QMX CAT       COMPLETE, hardware validated
T020  QMX CAT TX primitives                         COMPLETE, RF validation deferred to T022
```

Current AutoSeq already produces a pure semantic `AutoSeqTxIntent`; the current
controller still performs only simulated completion.

## Source of truth

Read before editing:

```text
AGENTS.md
docs/MiniFT8/architecture.md
docs/MiniFT8/development.md
docs/project/progress.md

apps/ft8/src/auto_seq/auto_seq_tx_intent.h
apps/ft8/src/auto_seq/auto_seq_tx_intent.c
apps/ft8/src/app_controller/app_controller_tx.c
apps/ft8/src/tx_lifecycle/tx_lifecycle.h

apps/ft8/src/ft8_engine/ft8_message_codec.c
apps/ft8/src/ft8_engine/ft8_message_codec.h
apps/ft8/src/ft8_engine/ft8_crc.c
apps/ft8/src/ft8_engine/ft8_crc.h
apps/ft8/src/ft8_engine/ft8_ldpc.c
apps/ft8/src/ft8_engine/ft8_ldpc.h
```

Pinned V2 references:

```text
wcheng95/Mini-FT8
491e757ae6b1e4cfd2b9a6ba10f48b35643849e0

main/autoseq.cpp                    # TX text projection
components/ft8_lib/ft8/message.c    # 77-bit message packing
components/ft8_lib/ft8/message.h
components/ft8_lib/ft8/encode.c     # CRC/LDPC -> channel tones
components/ft8_lib/ft8/encode.h
components/ft8_lib/ft8/constants.c
components/ft8_lib/ft8/constants.h

tests/tx_e2e/test_l1_encoder.cpp
tests/tx_e2e/gen_golden.cpp
tests/tx_e2e/golden/MANIFEST.txt
```

Do not vendor the old library wholesale. Port the required pure algorithms into
the current V3 ownership structure.

## Architectural constraints

1. T021 is pure C and platform-independent.
2. No MiniShell API include in the TX encoder/tone-plan module.
3. No Serial, CAT, radio_control, Audio, filesystem, time, or UI dependency.
4. No heap allocation.
5. AutoSeq remains semantic policy only; do not move encoding into AutoSeq.
6. `app_controller` is not changed to transmit in T021.
7. Existing RX decoder behavior remains unchanged.
8. Reuse current V3 codec/CRC/hash helpers where appropriate instead of creating
   a second unrelated protocol implementation.
9. A required FT8 LDPC generator table may be ported from the pinned V2 constants;
   do not alter the decoder's parity-check behavior merely to avoid that table.
10. Do not add FT4. T021 is FT8 only.

## Output contract

Add a small MiniFT8-owned TX encoding module, for example:

```text
apps/ft8/src/tx_encoder/
    tx_encoder.c
    tx_encoder.h
```

Exact naming may differ, but ownership must be explicit.

A suitable plan shape is:

```c
#define FT8_TX_TONE_COUNT 79u
#define FT8_TX_SYMBOL_PERIOD_MS 160u

typedef struct {
    char canonical_text[64];
    uint8_t payload[10];
    uint8_t tones[FT8_TX_TONE_COUNT];
    int16_t base_hz;
    uint8_t tx_parity;
} Ft8TxPlan;
```

The exact public-internal fields may differ, but T022 must be able to obtain:

- canonical TX text for diagnostics/UI;
- exact 10-byte packed payload;
- all 79 tone indices;
- base audio offset from `AutoSeqTxIntent.offset_hz`;
- TX parity;
- constants 160 ms and 6.25 Hz.

A helper that computes:

```text
tone_hz = base_hz + tone_index * 6.25
```

is acceptable, but must remain pure and clock-free.

## Semantic intent -> TX text

Preserve the pinned V2 TX text rules.

### Normal QSO

For a normal QSO intent:

```text
TX1  <DXCALL> <MYCALL> <MYGRID>
TX2  <DXCALL> <MYCALL> <REPORT>
TX3  <DXCALL> <MYCALL> R<REPORT>
TX4  <DXCALL> <MYCALL> RR73
TX5  <DXCALL> <MYCALL> 73
```

Report formatting follows V2 `%+d` semantics.

Examples:

```text
W1ABC K9XYZ FN42
W1ABC K9XYZ -12
W1ABC K9XYZ R-08    # canonical decoder may normalize equivalent report spelling
W1ABC K9XYZ RR73
W1ABC K9XYZ 73
```

Do not silently substitute a different message if a required QSO field cannot be
encoded.

### Field Day

When `AUTO_SEQ_TX_INTENT_FLAG_FD` is set:

```text
TX2  <DXCALL> <MYCALL> <FD_EXCHANGE>
TX3  <DXCALL> <MYCALL> R <FD_EXCHANGE>
TX4  <DXCALL> <MYCALL> RR73
TX5  <DXCALL> <MYCALL> 73
```

Use the FT8 ARRL Field Day message type for TX2/TX3, not free-text fallback.

### CQ

Map current CQ kinds:

```text
CQ          CQ <MYCALL> <GRID>
CQ SOTA     CQ SOTA <MYCALL> <GRID>
CQ POTA     CQ POTA <MYCALL> <GRID>
CQ QRP      CQ QRP <MYCALL> <GRID>
CQ FD       CQ FD <MYCALL> <GRID>
CQ FREETEXT intent.text
```

### Free text

`AUTO_SEQ_TX_INTENT_FREETEXT` transmits `intent.text` using FT8 free-text
encoding. Preserve the FT8/V2 normalization rules: lowercase becomes uppercase,
consecutive whitespace is normalized, and the result must fit the supported
13-character free-text payload.

## Message encoder scope

T021 must support every message form generated by the current AutoSeq intent
contract above:

- standard CQ/QSO messages;
- V2-style CQ modifiers used by current config;
- free text;
- ARRL Field Day TX2/TX3;
- RR73/73.

For this first integrated QSO path, standard callsigns are required.

If a semantic intent requires a nonstandard/hashed-call form that the T021 port
does not yet support, return a clear encode error and produce no valid plan.
Do **not** silently fall back to free text for a QSO message.

Nonstandard/hash TX support may be added later once the first standard-call QSO
path is complete.

## FT8 channel encoder

Port the pinned FT8 channel encoder behavior:

1. 77-bit payload;
2. CRC-14 -> 91 bits;
3. LDPC(174,91) systematic codeword;
4. split data into 3-bit values;
5. apply FT8 Gray map:
   ```text
   0 1 3 2 5 6 4 7
   ```
6. insert Costas:
   ```text
   3 1 4 0 6 5 2
   ```
   at:
   ```text
   0..6
   36..42
   72..78
   ```

Every output tone must be 0..7.

No waveform synthesis is needed for CAT-controlled QMX TX.

## Validation vectors

Tests must contain fixed expected V2-derived vectors checked into this repository.
Do not require the V2 repository at test runtime.

At minimum derive and lock exact payload + 79-tone vectors for:

```text
CQ W1XYZ FN42
W1ABC K9XYZ FN42
W1ABC K9XYZ -12
W1ABC K9XYZ R-08
W1ABC K9XYZ RR73
W1ABC K9XYZ 73
```

Also add deterministic semantic tests for:

- CQ SOTA;
- CQ POTA;
- CQ QRP;
- CQ FD;
- one free-text message;
- Field Day TX2;
- Field Day TX3.

Where a current V3 decoder supports the type, encoded payload should decode back
to the expected canonical message.

## AutoSeq intent projection tests

Construct `AutoSeqTxIntent` values directly and prove:

- TX1..TX5 text mapping;
- report propagation;
- base `offset_hz` copied into plan;
- parity copied unchanged;
- CQ type selection;
- free text;
- Field Day exchange;
- invalid/missing required fields fail;
- invalid message encoding leaves output invalid/cleared.

Do not drive the live AutoSeq queue or controller merely to test the encoder.

## Tone-plan tests

Prove:

- exactly 79 tones;
- every tone is <=7;
- all three Costas groups exactly match V2;
- deterministic vectors match byte-for-byte;
- tone-frequency mapping at base 1500 is exactly the eight FT8 offsets:
  ```text
  1500.00
  1506.25
  1512.50
  1518.75
  1525.00
  1531.25
  1537.50
  1543.75
  ```
  according to tone index 0..7.

## Non-goals

Do not implement:

- CAT calls;
- QMX TX;
- real RF;
- T020 hardware validation;
- slot clock/scheduling;
- late-entry/skip-tone logic;
- AutoSeq state advancement after physical TX;
- logging changes;
- Audio TX;
- FT4;
- UI changes;
- CAT endpoint defaults;
- nonstandard/hashed-call TX unless required by the chosen minimal encoder design
  and fully tested.

## Acceptance criteria

Software gate:

- [ ] current AutoSeq semantic intents project to correct FT8 TX text;
- [ ] 77-bit payload encoding matches pinned V2 vectors;
- [ ] FT8 CRC/LDPC/channel tones match pinned V2 vectors;
- [ ] output is exactly 79 tone indices;
- [ ] Costas groups are correct;
- [ ] all tone values are 0..7;
- [ ] TX1..TX5 covered;
- [ ] CQ variants covered;
- [ ] free text covered;
- [ ] Field Day TX2/TX3 covered;
- [ ] base offset/parity carried into immutable plan;
- [ ] no heap;
- [ ] no MiniShell/platform/radio dependency;
- [ ] existing RX decode tests remain green;
- [ ] Linux full CTest passes;
- [ ] portable unit suite passes;
- [ ] architecture checks pass;
- [ ] real ADV build passes;
- [ ] no unrelated cleanup.

There is **no hardware/manual acceptance step for T021**.

## Automated tests

Run:

```bash
git status --short

cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure

cmake -S tests/unit -B /tmp/T021-build-unit
cmake --build /tmp/T021-build-unit -j"$(nproc)"
ctest --test-dir /tmp/T021-build-unit --output-on-failure

PYTHONDONTWRITEBYTECODE=1 python3 tests/app_dependency_boundary.py . ft8
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_platform_boundary.py . ft8
PYTHONDONTWRITEBYTECODE=1 python3 tests/ft8_platform_boundary.py .

source ~/projects/esp-idf/export.sh
idf.py -C platform/adv build

git diff --check
```

No GitHub Actions wait.

## T022 inherited hardware requirement

T022 will integrate the T021 plan with:

```text
TxLifecycle slot boundary
AutoSeqTxIntent
T021 Ft8TxPlan
T020 radio_control begin / TA / end
```

Because T020 standalone RF testing was skipped, T022 hardware acceptance must
explicitly include:

- actual QMX keying;
- CAT TA tone control;
- bounded end-TX RX restoration;
- post-TX live receive recovery;
- and then the first real FT8 exchange/QSO.

T021 must not pre-empt that integration.

## Branch workflow

Use:

```text
codex/T021-ft8-tx-encoder
```

Codex:

1. read this task and pinned V2 encoder/text references;
2. implement only pure TX text/payload/tone-plan generation;
3. generate fixed V2 reference vectors once and check them into tests;
4. run all required local gates;
5. set Status to REVIEW;
6. record exact files, vector provenance, and results;
7. commit and push one reviewable implementation commit;
8. return commit SHA;
9. no PR;
10. no Actions wait.

## Codex implementation notes

Codex fills this section before handoff.

### Implementation summary

### Files changed

### Reference-vector provenance

### Invariants preserved

### Local tests run

### Known limitations / risks

### Commit

## Supervisor review

Supervisor reviews the actual `main..<commit>` diff and fixed vectors. Since
T021 is pure/deterministic and has no hardware gate, a clean review plus required
tests is sufficient for COMPLETE and fast-forward to main.

## Architect test result

No separate architect hardware test is required for T021. Record any deliberate
vector/semantic acceptance note here if needed.

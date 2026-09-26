# T082 — MiniFT8 RX page reset + configurable CQ modifier list

Status: REVIEW

## Architect intent

Make two small MiniFT8 usability changes without disturbing the accepted T081 RX
lifecycle or FT8 DSP:

1. every newly completed RX batch returns the RX UIScreen to page 1;
2. replace the hard-coded `CQ / CQ POTA` choice with a configurable ordered
   list of FT8-valid CQ modifiers from `setting.txt`.

Keep both changes local and simple.

---

## 1. New RX batch resets RX page to 1

### Required behavior

Today, if the operator pages RX to page 2/3 and a new `RxBatch` arrives, RX
remains on the previously selected page.

Change that behavior:

```text
new completed RxBatch
    -> RX page_index = 0
    -> RX page shown as 1/N
```

This applies to every newly completed RX batch, including a zero-message batch.

Do not reset paging merely because the UI model is rebuilt. Reset only when the
RX batch generation changes.

### Scope / ownership

Paging remains owned by `ui_shell`.

Expose enough factual identity in `UiModel` for `ui_shell` to detect a new RX
batch, for example the controller's existing RX batch/display generation.

Suggested model:

```text
UiModel
    rx_generation

UiShell
    last_rx_generation
```

When a new generation is observed:

- if RX is the current screen, set RX page to page 1;
- reset RX selected line to the first row as appropriate;
- do not disturb TX paging or another UIScreen's paging state.

Switching UIScreens continues to use the existing rule that the destination
screen opens at its top level/page 1.

### Non-goals

Do not change:

- RX ordering;
- RX message selection semantics;
- TX paging;
- V -> QSO paging;
- T081 batch generation semantics;
- UI top-line format.

---

## 2. Configurable CQ modifier choices

### User-facing configuration

Add a new MiniFT8 `setting.txt` field:

```text
cqtypes=POTA SOTA DX 250
```

The value is an ordered, space-separated list of CQ modifiers.

Plain:

```text
CQ
```

is always available implicitly as the first choice and is not required in the
`cqtypes=` line.

Therefore the example above produces this O -> 4 -> 1 cycle:

```text
CQ
CQ POTA
CQ SOTA
CQ DX
CQ 250
```

Preserve configured order.

Left selects the previous choice, Right selects the next choice, Enter advances
to the next choice, and navigation wraps.

### FT8-valid modifier rule

Use the FT8 standard-message CQ token rule already implemented by
`ft8_message_codec`.

A `cqtypes=` token is valid after uppercase normalization when it is exactly:

```text
1..4 letters A-Z
OR
exactly 3 decimal digits
```

Examples:

```text
POTA     valid
SOTA     valid
DX       valid
QRP      valid
FD       valid
250      valid

ABCDE    invalid
25       invalid
2500     invalid
DX1      invalid
P0TA     invalid
POTA/    invalid
```

Ignore invalid tokens. Do not reject the whole `setting.txt` because one
`cqtypes` entry is invalid.

Normalize alphabetic tokens to uppercase.

Ignore duplicate valid tokens after the first occurrence, preserving first
occurrence order.

If `cqtypes=` is absent, empty, or contains no valid tokens, plain `CQ`
remains available.

### Default compatibility

Keep the old useful built-in choices available when no explicit `cqtypes=` is
present by using a default modifier list compatible with the existing numeric
CQ type ordering:

```text
SOTA POTA QRP FD
```

This preserves the established `cq_type=2` -> `CQ POTA` behavior for existing
settings that have not added `cqtypes=`.

### Selected option persistence

Keep the existing numeric:

```text
cq_type=N
```

as the selected option index.

Interpret it as:

```text
0          plain CQ
1..count   valid cqtypes entries in configured order
```

Clamp an out-of-range selection to plain CQ.

When the operator changes O -> 4 -> 1, persist the selected index through the
existing save path.

This intentionally means that editing/reordering `cqtypes=` changes what a
stored nonzero index refers to. The list is operator-owned ordered
configuration.

### UI model / rendering

Replace the current UI-only enum restriction:

```text
UI_CQ / UI_CQ_POTA
```

with generic current CQ display facts sufficient to render:

```text
CQ Type: CQ
CQ Type: CQ POTA
CQ Type: CQ SOTA
CQ Type: CQ DX
CQ Type: CQ 250
```

Do not hard-code each modifier in `ui_shell`.

The UI should receive:

- current display text or modifier;
- current option index;
- option count.

The configuration/application layer owns the option list and selection.
`ui_shell` owns only navigation/presentation.

---

## TX / AutoSeq behavior

The existing FT8 codec already supports generic standard-message CQ modifiers.
Do not duplicate a second incompatible CQ grammar.

Extend the AutoSeq/TX intent boundary only as needed so a selected generic
modifier reaches `tx_encoder`.

Required behavior:

```text
selected ""      -> CQ <MYCALL> <GRID>
selected POTA    -> CQ POTA <MYCALL> <GRID>
selected DX      -> CQ DX <MYCALL> <GRID>
selected 250     -> CQ 250 <MYCALL> <GRID>
```

Existing known modifiers must retain their existing semantics where applicable:

- `SOTA`
- `POTA`
- `QRP`
- `FD`

In particular, `FD` must not lose the existing Field Day intent/flag behavior.

A valid generic modifier such as `DX` or `250` has ordinary CQ AutoSeq
semantics; it changes the CQ token only.

Existing free-text CQ support remains separate. `FREETEXT` is not a generic
`cqtypes` token.

Preserve the T027 non-standard-local-callsign rule: this task does not expand
modified-CQ support for a non-standard local callsign beyond what the encoder
currently supports.

---

## Configuration representation

Keep storage bounded and allocation-free.

A compact fixed representation is preferred. The implementation may retain a
canonical filtered `cqtypes` string and provide indexed accessors, or store a
small fixed array of modifiers.

Do not add heap allocation.

The serialized `setting.txt` must include the canonical valid list, for
example:

```text
cq_type=2
cqtypes=POTA SOTA DX 250
```

Invalid/duplicate input entries should not reappear after the file is saved.

---

## Required tests

### RX page reset

1. create a multi-page RX batch;
2. move RX to page 2 or later;
3. rebuild the same generation -> page must stay where the user selected;
4. publish a new batch generation -> page becomes 1;
5. verify zero-message generation also resets to page 1;
6. verify a new RX generation while viewing TX does not disturb TX page;
7. switching back to RX still enters page 1 through the existing screen-entry
   rule.

### CQ list parsing

Cover at minimum:

```text
cqtypes=POTA SOTA DX 250
```

and verify exact option order:

```text
CQ, CQ POTA, CQ SOTA, CQ DX, CQ 250
```

Also test:

- lowercase normalization;
- repeated spaces;
- invalid entries ignored;
- duplicates ignored after first occurrence;
- missing/empty/all-invalid list leaves plain CQ;
- default list preserves old `cq_type=2` -> POTA behavior;
- out-of-range `cq_type` -> plain CQ;
- serialization emits only canonical valid unique modifiers.

### CQ UI navigation

Verify O -> 4 -> 1:

- Left previous with wrap;
- Right next with wrap;
- Enter next with wrap;
- rendered text uses the selected modifier;
- action persists selected index.

### CQ TX encoding

Using a standard local callsign, verify exact encode/decode round-trip for at
least:

```text
CQ
CQ POTA
CQ SOTA
CQ DX
CQ 250
```

Verify `FD` preserves existing Field Day semantics.

Preserve existing AutoSeq and TX encoder suites.

---

## Protected boundaries / non-goals

Do not change:

- T081 capture/decode scheduling or ownership;
- waterfall/FFT/candidate/LDPC code;
- RX batch content/order;
- AutoSeq QSO progression except carrying the configured CQ modifier;
- beacon parity behavior;
- CAT/physical TX lifecycle;
- TX offset policy;
- MiniShell public API;
- Filesystem/config location;
- UI top-line layout;
- QSO/log paging;
- free-text message editor scope.

No heap allocation.

---

## Likely files

```text
apps/ft8/include/ft8/app_types.h
apps/ft8/src/ui_shell/ui_shell.[ch]
apps/ft8/src/config_service/config_service.[ch]
apps/ft8/src/app_controller/app_controller.c
apps/ft8/src/auto_seq/auto_seq.[ch]
apps/ft8/src/auto_seq/auto_seq_tx_intent.[ch]
apps/ft8/src/tx_encoder/tx_encoder.c
focused UI/config/AutoSeq/TX tests
docs/MiniFT8/ui.md
docs/MiniFT8/README.md
```

Keep changes smaller if generic CQ intent can be carried without touching every
listed file.

---

## Acceptance

Software acceptance requires:

- every new RX batch resets RX to page 1;
- same-generation redraw does not reset user paging;
- `cqtypes=POTA SOTA DX 250` yields exactly
  `CQ / CQ POTA / CQ SOTA / CQ DX / CQ 250`;
- invalid CQ modifiers are silently ignored;
- configured order is preserved;
- selected CQ type persists;
- generic CQ modifiers encode correctly;
- existing POTA/SOTA/QRP/FD behavior remains valid;
- all relevant Linux/unit/architecture/ADV build gates pass.

Manual ADV acceptance:

1. receive enough messages for multiple RX pages;
2. move to page 2;
3. wait for a new RX batch;
4. verify RX returns to page 1;
5. set `cqtypes=POTA SOTA DX 250`;
6. restart FT8;
7. verify O -> 4 -> 1 cycles through
   `CQ, CQ POTA, CQ SOTA, CQ DX, CQ 250` in that order;
8. change selection, exit/re-enter FT8, and verify selected index persists;
9. optional RF confirmation of a generic modifier such as `CQ DX` or
   `CQ 250`; software encoder round-trip is the required gate.

---

## Codex branch / handoff

Work on:

```text
codex/T082-ft8-rx-page-cq-list
```

Start from current `main`.

Read:

```text
AGENTS.md
docs/MiniFT8/README.md
docs/MiniFT8/development.md
docs/MiniFT8/ui.md
this task packet
```

Keep T082 to these two usability changes. Do not start the deferred two-stage
decode, reply-to-me/CQ-priority decode, or deep-decode work.

Use one reviewable implementation commit. Do not merge to `main` and do not
open a PR unless asked.

## Codex implementation handoff

### Implementation summary

`UiModel.rx_generation` exposes the existing controller batch generation.
`ui_shell` observes it before rendering and input handling, resets RX page and
selected row only on a changed generation while RX is visible, and records it
without disturbing other screens. Same-generation redraws preserve the chosen
page. RX entry continues to start on page 1.

Configuration stores up to 16 unique modifiers in fixed five-byte entries.
Parsing normalizes letters, ignores invalid/duplicate/excess tokens, preserves
first occurrence order, and defers selected-index validation until the entire
file is parsed. An absent list defaults to `SOTA POTA QRP FD`; an explicit empty
or all-invalid list contains only implicit plain CQ. Serialization emits the
canonical list and numeric index. O -> 4 -> 1 navigates generic text/index/count
facts with previous/next wrapping and saves through the existing rollback-safe
controller path.

The standard CQ token grammar/n28 packing was extracted unchanged from
`ft8_message_codec.c` into the pure `cq_token.h` helper. Configuration, AutoSeq
modifier validation and the codec share it. Architecture rules allow only that
specific helper edge, without allowing configuration or AutoSeq to depend on
engine/UI modules generally.

AutoSeq retains its known CQ types and separate free-text CQ intent, adds a
bounded generic modifier, and maps known names back to their existing semantics.
FD still sets the Field Day intent flag/exchange. TX encoding carries generic
standard CQ tokens and retains T027's non-standard-local-call restrictions.

### Files changed

- `apps/ft8/include/ft8/app_types.h`, `src/ui_shell/ui_shell.[ch]`,
  `src/app_controller/app_controller.c`: generation-driven RX paging and generic
  CQ display/navigation/config synchronization.
- `apps/ft8/src/config_service/config_service.[ch]`: bounded ordered modifier
  list, filtering, default compatibility, index validation and serialization.
- `apps/ft8/include/ft8/cq_token.h`,
  `src/ft8_engine/ft8_message_codec.c`, `tests/architecture_rules.py`: one shared
  standard-CQ grammar and narrowly scoped dependency ownership.
- `apps/ft8/src/auto_seq/auto_seq.[ch]`, `auto_seq_tx_intent.[ch]`,
  `src/tx_encoder/tx_encoder.c`: generic CQ transport with known/free-text
  semantics preserved.
- `tests/ft8_cq_list_test.c`, `ft8_ui_smoke.c`, `ft8_physical_tx_test.c`,
  `linux_ft8_config_load.py`, `CMakeLists.txt`: parsing, codec, UI, controller
  persistence/rollback and default-file regression coverage.
- `docs/MiniFT8/README.md`, `ui.md`, and this task: operator behavior and handoff.

### Behavior/invariants preserved

- T081 capture/decode scheduling, worker ownership and batch-generation semantics
  are unchanged. Waterfall/FFT/candidate/LDPC and framer files match base main.
- RX content/order, selection mapping, TX/QSO paging, top-line layout, beacon
  parity, physical TX/CAT lifecycle and TX offset policy are unchanged.
- No MiniShell public API or config location changes; no heap allocation added.
- Default `cq_type=2` still selects POTA. Config indices are now intentionally
  independent of AutoSeq's semantic CQ enum.
- `cq_ft` storage and free-text CQ AutoSeq/encoder support remain separate.
  Per the packet's index contract, numeric 5 means the fifth configured modifier
  if present, otherwise plain CQ; it is no longer a special free-text selector.
- Existing tests were updated only for the authorized enum-to-list UI contract
  and the newly serialized default `cqtypes` field. Their persistence, rollback,
  beacon and physical-TX assertions remain in place.
- No task-scope deviations. The chosen bounded capacity is 16 modifiers; later
  valid tokens are ignored and this bound is documented for operators.

### Tests run and results

```text
cmake -S . -B build-linux
cmake --build build-linux -j8
    PASS; no compiler warnings in the Linux build log.

ctest --test-dir build-linux -R 'ft8_(cq_list|ui|physical_tx|auto_seq|tx_encoder)|linux_ft8_config_load' --output-on-failure
    PASS: 6/6.

PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure
    Final run PASS: 128/128 (62.32 s).
    Earlier run: 127/128; only the known serial timeout assertion failed.

ctest --test-dir build-linux -R 'ft8_(cq_list|ui|physical_tx|auto_seq|tx_encoder)|linux_ft8_config_load|^linux_serial_unit$' --output-on-failure
    All six FT8 checks PASS; known unrelated linux_serial_unit failure reproduced.

cmake -S tests/unit -B /tmp/T082-unit
cmake --build /tmp/T082-unit -j8
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir /tmp/T082-unit --output-on-failure
    PASS: 29/29.

python3 tests/app_dependency_boundary.py . ft8
python3 tests/app_platform_boundary.py . ft8
python3 tests/ft8_platform_boundary.py .
python3 tests/architecture_rules.py .
    PASS: all four commands, including purity/no-heap checks.

source /home/wei/projects/esp-idf/export.sh
idf.py -C platform/adv build
    PASS: firmware 0x1541a0 bytes; 78% of smallest app partition free.
    Existing SDK include_next/pedantic warnings remain.

git diff --check
    PASS.
```

New coverage checks exact CQ option order; normalization and repeated spaces;
invalid/duplicate/empty/missing/overflow lists; out-of-range indices and field
order independence; canonical save/reparse; all requested CQ encode/decode
round-trips plus FD/QRP; Field Day intent metadata; non-standard-call rejection;
separate free-text CQ; controller selection save/reload and failed-save rollback;
generic UI text and all navigation directions; new/same/empty RX generations;
TX page preservation; RX screen re-entry; and input before redraw.

### Hardware/manual validation still required

ADV acceptance from the task: multi-page RX returns to page 1 on a fresh batch;
`cqtypes=POTA SOTA DX 250` cycles in that order; selection survives exit/re-entry.
Optional RF confirmation of DX/250 remains for hardware testing. No device was
flashed and no RF transmission was performed for this handoff.

### Known limitations or risks

The previously observed unrelated serial PTY timeout flake remains at
`tests/linux_serial_test.c:67` (`write(...) == MINI_ERR_TIMEOUT && n == 0`).
Serial test/backend/service/public API files match base main; no serial changes
or weakened assertions were made. Hardware acceptance of T082 is still pending.
The modifier list is bounded at 16 entries, and reordering it deliberately
changes what a saved nonzero index selects.

### Commit reference

One review commit titled `T082: reset RX paging and configure CQ modifiers` on
`codex/T082-ft8-rx-page-cq-list`, based on current main
`10d14910210c9893a1d9dce0f89a338a0ed2e834`. The pushed SHA is supplied in the
Codex handoff; main is not merged and no PR is opened.

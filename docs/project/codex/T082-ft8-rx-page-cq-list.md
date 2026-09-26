# T082 — MiniFT8 RX page reset + configurable CQ modifier list

Status: READY

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

# T029 — RX display lifetime across TX and discontinuity

Status: COMPLETE

## Architect intent

Correct the T028 RX display-map lifetime.

The sorting/mapping itself is correct. The bug is that T028 tied display
invalidation to RX transport resets, which causes the previous decoded RX list to
disappear as soon as TX starts.

Required user-visible behavior:

- decoded RX messages stay visible/selectable until replaced by the next completed
  RX batch;
- if the next completed RX batch contains zero messages, the display becomes empty;
- if the next slot is TX, keep the previous RX list visible during the whole TX;
- when TX finishes and RX resumes, clear the previous RX list because that slot
  intentionally had no RX decode.

The goal is to avoid showing a prior-slot RX list after a TX slot has completed,
while still keeping useful context visible during the TX itself.

## Current regression

T028 added `rx_invalidate_order()` calls in:

- `app_controller_pause_rx_for_tx()`
- `app_controller_resume_rx_after_tx()`
- RX discontinuity handling
- RX slot-framer stream-reset handling

`rx_invalidate_order()` sets:

```c
display_count = 0;
selected_rx_valid = false;
```

Because `app_controller_build_ui_model()` projects RX lines only through the
display map, invalidating at TX start clears the RX screen immediately even
though the factual `RxBatch` still exists.

Before T028, TX pause/reset did not clear the previous decoded list.

## Canonical lifetime rule

### Normal RX-to-RX progression

A completed RX batch replaces the current displayed batch.

```text
old displayed RX batch
    -> next completed RX batch with messages
    -> replace with new sorted display

old displayed RX batch
    -> next completed RX batch with zero messages
    -> clear display
```

No wall-clock TTL is added.

### RX-to-TX progression

```text
RX batch decoded and displayed
    -> next slot begins TX
    -> keep previous RX list visible during TX
    -> TX completes
    -> RX resumes
    -> clear previous RX list
```

This clear is intentional because the just-finished slot was a TX slot and
there was no RX decode for it.

### Ordinary RX transport discontinuity/reset

A transport discontinuity/reset by itself does not represent a new decoded slot
and must not erase the currently displayed RX batch.

```text
displayed RX batch
    -> audio discontinuity / frontend reset / framer stream reset
    -> keep displayed RX batch
    -> later completed RX batch replaces it
```

## Ownership

Keep the T028 ownership split unchanged:

```text
RxBatch              factual order
app_rx_order          sorted display-index map only
app_controller        display-map lifetime and manual-selection mapping
ui_shell              render/paginate only
AutoSeq               TX/QSO policy only
```

Do not move lifetime policy into `ui_shell`.

## Required implementation

The smallest expected production change is:

1. do not call `rx_invalidate_order()` from `app_controller_pause_rx_for_tx()`;
2. retain clearing when TX has finished and `app_controller_resume_rx_after_tx()`
   successfully resumes RX;
3. do not invalidate the displayed map solely because
   `RX_AUDIO_ADAPTER_DISCONTINUITY` occurred;
4. do not invalidate the displayed map solely because the slot framer emits
   `RX_SLOT_FRAMER_EVENT_STREAM_RESET`;
5. continue to rebuild/replace the map in `rx_complete_batch()`.

If implementation details require a small helper split, keep it private and
bounded. Do not redesign T028 sorting.

## Selection lifetime

Manual selection must track the same visible batch lifetime.

- while the old RX list is visible before/during TX, its display mapping remains
  valid;
- existing TX-active action-freeze behavior is not changed by this task;
- when TX completes/resumes RX and the list is cleared, invalidate
  `selected_rx_valid`;
- when a new completed RX batch arrives, existing T028 behavior resets prior
  selection and publishes the new mapping.

Do not queue new manual actions during an active TX in this task.

## Important distinction

Do not confuse these:

```text
RX transport state
    active / paused / reset / discontinuity

RX display state
    most recent completed decode batch and its sorted index map
```

A transport reset does not automatically invalidate an already completed decode.

TX completion is the special explicit clear point because the completed slot had
no RX decode.

## Tests

Add/update focused controller regressions proving:

### A. TX start preserves display

Given a completed RX batch with at least two messages:

- record the sorted `UiModel.rx_lines`;
- start physical/mock TX so `app_controller_pause_rx_for_tx()` is exercised;
- while TX is active:
  - `model.rx_count` is unchanged;
  - same displayed lines remain in the same order;
  - `display_generation == batch_generation`;
  - `display_count` is unchanged.

### B. TX completion clears display

Advance all 79 symbols through normal successful TX completion.

After `app_controller_resume_rx_after_tx()`:

- `model.rx_count == 0`;
- `display_count == 0`;
- `selected_rx_valid == false`;
- factual `RxBatch` storage/history may remain intact;
- no new RX batch is fabricated.

### C. Discontinuity preserves display

Given a completed visible RX batch:

- inject `RX_AUDIO_ADAPTER_DISCONTINUITY`;
- verify frontend/timing reset semantics still occur;
- verify displayed RX count/order remain unchanged;
- verify the old display mapping remains usable until another real batch is
  completed.

Likewise for the framer stream-reset event: engine/stream state resets, but the
display snapshot remains.

### D. New completed batch replaces display

After preserved old display:

- complete a new batch with a deliberately different sorted order;
- assert the new batch replaces the old display exactly once.

Also test a zero-message completed batch clears the display.

## Regressions to preserve

- T028 group/SNR ordering and stable ties;
- T028 display-index -> factual-index selection mapping;
- raw `RxBatch` order remains unchanged;
- automatic addressed processing and RT logging remain raw-batch order;
- T026 RR73 behavior;
- T027 non-standard/hash TX behavior;
- physical mocked-QMX scheduling/timing;
- RX discontinuity engine/hash-history semantics;
- RX pagination;
- Linux/ADV presentation behavior.

## Non-goals

Do not change:

- RX sorting keys or algorithm;
- decoder/RxResultBuilder order;
- AutoSeq policy;
- TX scheduling/tone generation;
- TX-active action-freeze behavior;
- UI colors/style/layout;
- protocol behavior;
- SNR computation;
- logging formats;
- public MiniShell APIs;
- Linux Serial implementation or `linux_serial_unit`.

The known intermittent GitHub CI `linux_serial_unit` timeout from Linux runs
#933/#934 is explicitly deferred and must not be modified in T029.

## Acceptance criteria

- [x] previous decoded RX list remains visible throughout TX;
- [x] previous RX list clears only after successful TX completion/resume;
- [x] ordinary RX audio discontinuity does not clear displayed messages;
- [x] framer stream reset does not clear displayed messages;
- [x] next completed RX batch replaces previous display;
- [x] zero-message completed batch clears previous display;
- [x] selection validity follows the same display lifetime;
- [x] T028 sorting and selection mapping remain unchanged;
- [x] raw factual batch is never reordered;
- [x] no wall-clock TTL is introduced;
- [x] no color/style change;
- [x] Linux CTest passes except any separately reproduced known serial flake is
      documented rather than patched here;
- [x] portable units pass;
- [x] architecture checks pass;
- [x] ADV build passes;
- [x] git diff --check passes;
- [x] no unrelated cleanup.

## Automated tests

Run:

```bash
git status --short

cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure

cmake -S tests/unit -B /tmp/T029-build-unit
cmake --build /tmp/T029-build-unit -j"$(nproc)"
ctest --test-dir /tmp/T029-build-unit --output-on-failure

PYTHONDONTWRITEBYTECODE=1 python3 tests/app_dependency_boundary.py . ft8
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_platform_boundary.py . ft8
PYTHONDONTWRITEBYTECODE=1 python3 tests/ft8_platform_boundary.py .
PYTHONDONTWRITEBYTECODE=1 python3 tests/architecture_rules.py .

source ~/projects/esp-idf/export.sh
idf.py -C platform/adv build

git diff --check
```

If `linux_serial_unit` alone fails with the already-known line-67 timeout
assertion, record that exact evidence and rerun/continue the relevant focused T029
gates. Do not change Serial code/test in this task.

## Manual validation

No RF transmission requirement beyond the existing physical/mock path.

Architect live check:

1. decode one RX slot;
2. enter a TX slot;
3. confirm old RX messages stay on screen during TX;
4. when TX ends, confirm RX screen clears;
5. after next RX decode, confirm new sorted messages appear normally.

## Branch workflow

Use:

```text
codex/T029-rx-display-lifetime
```

Codex:

1. read T028 and this correction;
2. reproduce the current clear-at-TX-start regression;
3. make the smallest lifetime fix;
4. add focused lifecycle regressions;
5. run required gates;
6. set Status to REVIEW;
7. record before/after evidence;
8. push one reviewable implementation commit;
9. return exact SHA;
10. no PR;
11. no Actions wait.

## Codex implementation notes

### Implementation summary

Removed four production lines: display-map invalidation at TX pause, audio
discontinuity and framer stream reset, plus the discontinuity-only model-change
notification. These transport events retain the completed display snapshot and
selection. Existing successful RX-resume clearing and completed-batch publication
remain unchanged. No helper split or sorting redesign was needed.

### Files changed

- `apps/ft8/src/app_controller/app_controller.c`: minimal lifetime correction.
- `tests/ft8_physical_tx_test.c`: physical TX lifetime regression and corrected
  framer-reset/replacement expectations.
- `tests/ft8_rx_discontinuity_test.c`: retain two sorted visible rows across audio
  gaps and framer reset; verify mapping remains usable and a completed silent
  window replaces the display with an empty batch.
- This task packet: evidence and handoff.

### Invariants preserved

Controller retains lifetime ownership; factual batches remain untouched. T028
sort keys, generation checks, display-to-factual mapping and pagination are
unchanged. New completed batches reset prior selection and publish a new map;
empty completed batches clear it. Existing active-TX action freeze is asserted:
a selection attempted during TX does not change AutoSeq or selected factual index.

The physical regression checks both sorted lines at every symbol interval,
matching display/batch generation and valid prior selection through active TX.
At normal completion after 79 symbols, RX is active, the display/selection are
cleared, and factual messages, count and generation remain intact: no decode batch
is fabricated. Existing successful-resume and failure-recovery semantics are
unchanged. T026/T027, raw-order RT/AutoSeq, engine/hash-history, Linux/ADV profiles
and T028 order tests pass. No heap, TTL, styling, API or protocol change.

### Failing-before / passing-after evidence

Added the physical lifecycle regression before production edits and ran:

```bash
cmake -S . -B build-linux
cmake --build build-linux --target ft8_physical_tx_unit -j8
build-linux/ft8_physical_tx_unit
```

Before (exit 134):

```text
RX lifetime: before=2 during TX=0
Assertion `during.rx_count==before.rx_count' failed.
```

After:

```text
RX lifetime: before=2 during TX=2
```

Full physical test and all 79-symbol display assertions pass. Old T028 assertions
that reset/discontinuity immediately empties the screen were updated narrowly to
the architect-approved T029 lifetime. Generation-mismatch rejection, ordering,
raw batch preservation and engine reset/hash-history assertions remain intact.

### Local tests run

```bash
git status --short
cmake -S . -B build-linux
cmake --build build-linux -j8
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure
# 58/59 PASS; only documented linux_serial_unit flake failed (see below)
ctest --test-dir build-linux --output-on-failure -R '^linux_serial_unit$'
# PASS 1/1 on isolated retry
ctest --test-dir build-linux --output-on-failure -R 'ft8_physical_tx|ft8_rx_discontinuity|ft8_rx_adv_profile|ft8_rx_order'
# PASS 4/4
cmake -S tests/unit -B /tmp/T029-build-unit
cmake --build /tmp/T029-build-unit -j8
ctest --test-dir /tmp/T029-build-unit --output-on-failure
# PASS 15/15
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_dependency_boundary.py . ft8
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_platform_boundary.py . ft8
PYTHONDONTWRITEBYTECODE=1 python3 tests/ft8_platform_boundary.py .
PYTHONDONTWRITEBYTECODE=1 python3 tests/architecture_rules.py .
# All PASS
source ~/projects/esp-idf/export.sh
idf.py -C platform/adv build
# PASS real ESP32-S3 build; binary 0xbc910 bytes, 88% app partition free
git diff --check
# PASS
```

Exact separately reproduced known serial flake from the full run:

```text
67: api->write(stream, large, sizeof(large), &n, 20) == MINI_ERR_TIMEOUT && n == 0
```

Serial implementation and test are untouched as required. No GitHub Actions wait.

### Manual validation still required

No RF or hardware transmission performed. Architect should confirm on live
Linux/QMX that old sorted RX rows remain through TX, clear at TX end/RX resume,
and are replaced normally after the next RX decode.

### Known limitations / risks

Live visual validation remains outstanding. The known intermittent Serial test
is deferred exactly as specified and passed its isolated retry. This task retains
the existing successful `app_controller_resume_rx_after_tx()` clear boundary;
it does not alter failure recovery or queue policy. No task deviations.
Existing untracked Python cache directories were left alone.

### Commit

One implementation commit on `codex/T029-rx-display-lifetime`, based on task head
`2a1c5e1`. The commit containing these notes is the implementation reference;
exact SHA is returned in the handoff. No PR.

## Supervisor review

PASS on implementation commit `100b1b739cfd082f4ed288a6cf65e1e03d0c4879`.

Reviewed the exact single implementation commit from task head
`2a1c5e16c9aa4935b81b2b4a5cfa52d370846d6e`. No blocking finding.

The production change is intentionally minimal: four removed lines in
`app_controller.c`.

Accepted lifetime behavior:

```text
completed RX batch
    -> display/order map published

TX starts / RX audio pauses
    -> preserve displayed RX rows and selection mapping

TX active through all 79 symbols
    -> preserve same displayed RX rows

TX completes successfully / RX resumes
    -> existing rx_invalidate_order() clears display + selection

ordinary audio discontinuity
    -> reset RX transport/timing only
    -> preserve completed display snapshot

framer STREAM_RESET
    -> reset engine stream only
    -> preserve completed display snapshot

next completed RX batch
    -> replace previous display map
    -> empty completed batch clears display
```

This matches the architect rule: the most recent completed RX decode remains
visible/clickable until a replacement decode arrives, except that a completed TX
slot explicitly clears the old RX display when RX resumes because that slot had no
RX decode.

Accepted invariants:

- T028 sorting keys and index mapping are unchanged;
- factual `RxBatch` is not reordered or destroyed;
- `display_generation` remains tied to the factual batch generation;
- active-TX action freeze is unchanged;
- successful resume remains the sole TX lifecycle clear point;
- discontinuity still resets frontend/timing and framer/engine state correctly;
- next completed batch resets prior selection and publishes a new mapping;
- zero-message completed batch clears the display naturally;
- no wall-clock TTL, heap, style, protocol, logging, or public API change.

Accepted regression evidence:

```text
before T029:
RX lifetime: before=2 during TX=0
assertion during.rx_count == before.rx_count failed

after T029:
RX lifetime: before=2 during TX=2
all 79 active-TX symbol checks preserve the display
TX completion/resume clears display and selection
```

Focused tests also prove transport discontinuity and framer reset preserve the
same sorted visible rows and selectable mapping until a real completed batch
replaces them.

Accepted local evidence:

```text
focused T029 tests    4/4 PASS
portable units       15/15 PASS
architecture checks  PASS
real ADV build       PASS
git diff --check     PASS
```

Full Linux CTest was 58/59 only because of the previously documented
`linux_serial_unit` line-67 PTY timeout flake. The isolated retry passed 1/1.
No Serial source or test changed in T029, so this is non-blocking and remains
deferred.

T029 is TESTING. Remaining acceptance is the live visual check:

1. previous sorted RX rows remain visible throughout TX;
2. they clear when TX finishes / RX resumes;
3. the next RX decode populates the newly sorted list normally.


## Architect test result

PASS. Live Linux/QMX validation completed successfully.

Accepted observed behavior:

- previous sorted RX rows remain visible throughout TX;
- they clear when TX finishes and RX resumes;
- the next RX decode repopulates the sorted list normally;
- ordinary RX transport resets do not prematurely clear the displayed snapshot.

T029 is COMPLETE.

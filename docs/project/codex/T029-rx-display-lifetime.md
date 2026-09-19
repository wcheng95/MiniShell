# T029 — RX display lifetime across TX and discontinuity

Status: READY

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

- [ ] previous decoded RX list remains visible throughout TX;
- [ ] previous RX list clears only after successful TX completion/resume;
- [ ] ordinary RX audio discontinuity does not clear displayed messages;
- [ ] framer stream reset does not clear displayed messages;
- [ ] next completed RX batch replaces previous display;
- [ ] zero-message completed batch clears previous display;
- [ ] selection validity follows the same display lifetime;
- [ ] T028 sorting and selection mapping remain unchanged;
- [ ] raw factual batch is never reordered;
- [ ] no wall-clock TTL is introduced;
- [ ] no color/style change;
- [ ] Linux CTest passes except any separately reproduced known serial flake is
      documented rather than patched here;
- [ ] portable units pass;
- [ ] architecture checks pass;
- [ ] ADV build passes;
- [ ] git diff --check passes;
- [ ] no unrelated cleanup.

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

### Files changed

### Invariants preserved

### Failing-before / passing-after evidence

### Local tests run

### Manual validation still required

### Known limitations / risks

### Commit

## Supervisor review

Review exact task-head to implementation diff.

## Architect test result

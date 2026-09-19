# T028 — RX message ordering for display and selection

Status: READY

## Architect intent

Add V2-style decoded-message priority ordering to MiniFT8-V3, owned by
app_controller as a small private module.

Required visible group order:

1. reply-to-me
2. CQ
3. regular

Within every group, sort by received strength from strongest to weakest.
Do not add color coding in this task.

## Objective

Keep the factual RxBatch in decoder/result-builder order, but give the RX
UIScreen a controller-owned display/selection order.

The row selected by the operator must map back to the correct original RxMessage.

## Current behavior

Current path:

Ft8Engine -> Ft8ProtocolSlot -> RxResultBuilder -> RxBatch
          -> app_controller_build_model -> UiModel -> ui_shell

Today app_controller_build_model copies batch.messages[i] directly to UI index i.
APP_ACTION_SELECT_RX_MESSAGE also treats the UI index as the raw batch index.

The same factual batch is consumed by app_process_addressed_batch for RT logging
and automatic addressed-message processing. Presentation sorting must not alter
that processing order.

## V2 reference

Pinned reference:
wcheng95/Mini-FT8
491e757ae6b1e4cfd2b9a6ba10f48b35643849e0
main/main.cpp

V2 uses:
- group 0 when is_to_me
- group 1 otherwise when is_cq
- group 2 otherwise

Thus is_to_me takes precedence over is_cq.

V2 sorts only the CQ group by SNR. T028 deliberately extends that behavior:
- reply-to-me: strongest to weakest
- CQ: strongest to weakest
- regular: strongest to weakest

Strength is RxMessage.snr_db; numerically larger is stronger.
For equal SNR, preserve original RxBatch order.

## Ownership

Create a small private controller module, preferably:

apps/ft8/src/app_controller/app_rx_order.c
apps/ft8/src/app_controller/app_rx_order.h

Exact naming may differ, but ownership must remain under app_controller.

The module must be pure:
- no MiniShell API
- no UI rendering
- no AutoSeq calls
- no filesystem/time/audio/radio
- no heap allocation

It consumes factual RxMessage entries and produces only an index ordering.

## Required design

Do not sort RxBatch.messages in place.

The controller RX state should maintain a bounded mapping equivalent to:

size_t display_order[FT8_DECODER_CANDIDATE_CAPACITY];
size_t display_count;

For this factual batch:

batch[0] = regular, -5 dB
batch[1] = CQ,      -10 dB
batch[2] = to-me,   -18 dB
batch[3] = CQ,       +2 dB
batch[4] = to-me,    -3 dB
batch[5] = regular, +10 dB

the display map must be:

[4, 2, 3, 1, 5, 0]

The original batch remains unchanged.

## Exact sorting key

Primary group:
0 if is_to_me
1 else if is_cq
2 otherwise

Secondary:
snr_db descending

Tertiary:
original batch index ascending

No other field affects ordering.

Do not sort by offset, protocol type, grid/report stage, text, callsign,
candidate score, LDPC errors, selectability, or AutoSeq state.

## Batch lifecycle

Build/reset the display map whenever a new completed RxBatch replaces the old one.
On RX/stream reset, stale ordering must not be usable.
The map must remain associated with the same batch generation.

No dynamic re-sort is required between completed decode batches.

## UI projection

app_controller_build_model must fill UiModel.rx_lines in display order:

display index -> display_order -> factual batch index -> canonical_text

UiModel.rx_count remains the complete bounded batch count up to APP_MAX_RX_LINES
(50), preserving current pagination.

Do not move sorting policy into ui_shell. ui_shell continues to render/paginate an
already ordered UiModel.

## Manual selection mapping

APP_ACTION_SELECT_RX_MESSAGE receives the displayed/global paginated index from
ui_shell.

Before reading RxMessage, map:

display index -> original factual batch index

Then preserve all existing selection semantics.

Prefer selected_rx_index to continue meaning the factual batch index after mapping.

## Automatic processing and logging

Do not change app_process_addressed_batch iteration.

It must continue to walk the original factual batch order for:
- RT logging
- addressed-message projection
- AutoSeq input

Display sorting is presentation/manual-selection policy only.

## Classification source

Use only:
RxMessage.is_to_me
RxMessage.is_cq
RxMessage.snr_db

Do not parse canonical_text for grouping.

This must naturally preserve existing standard, Field Day, type-4/non-standard,
resolved-hash, and free-text logical CQ classification from RxResultBuilder.

## Tie behavior

Equal SNR within a group preserves original batch order explicitly via the
original index as final comparison key.

Do not rely on sort-library stability.

## Algorithm and memory

Maximum batch size is 50.

Use fixed storage and a simple deterministic algorithm. Insertion sort over the
index array is acceptable. O(50^2) is fine.

No heap and no generalized sorting framework are needed.

## Color/style

Out of scope.

Do not add:
- RX colors
- group separators/labels
- icons
- SNR formatting changes
- new selection styling

This task changes order only.

## Focused module tests

At minimum prove:
1. mixed three-group order
2. strongest-to-weakest in reply-to-me
3. strongest-to-weakest in CQ
4. strongest-to-weakest in regular
5. equal-SNR original-index order
6. is_to_me plus is_cq belongs to reply-to-me
7. empty input
8. one entry
9. full 50-entry bounded input
10. input RxMessage array is not modified

## Controller integration tests

Use a completed factual batch with deliberately scrambled order.

Prove RxBatch order differs from UiModel order and assert exact display order.

Then select at least one reordered line and prove the action reaches the correct
original message / AutoSeq event.

Required selection example:

raw batch:
[0] weak CQ station A
[1] strong CQ station B

display:
[0] strong station B
[1] weak station A

select display [0]
-> AutoSeq DX must be station B, not raw batch[0]

Also prove a reply-to-me message remains ahead of all CQs even when weaker.

## Regression requirements

Preserve:
- automatic addressed-message processing
- RT RX log content/count
- T026 RR73 behavior
- T027 non-standard CQ/reply path
- RX pagination
- AutoSeq TX queue ordering
- physical TX
- Linux and ADV presentation profiles

No decoder, RxResultBuilder, AutoSeq, or ui_shell semantic redesign.

## Expected files

Likely:
apps/ft8/src/app_controller/app_rx_order.[ch]
apps/ft8/src/app_controller/app_controller.c
CMakeLists.txt
platform/adv/main/CMakeLists.txt
focused order/controller tests
this task packet

Minimize changes outside this set.

## Non-goals

Do not change:
- decoder candidate order
- RxResultBuilder output order
- SNR computation
- AutoSeq queue priority
- automatic reply policy
- FT8 protocol classification
- color coding
- UI layout/paging rules
- logging formats
- TX behavior
- configuration formats
- MiniShell public APIs

## Acceptance criteria

- controller owns RX display/selection ordering
- factual RxBatch is never reordered
- group order is reply-to-me, CQ, regular
- all three groups are SNR descending
- equal SNR preserves original order
- display and manual selection share the same mapping
- automatic addressed processing remains raw-batch order
- all 50 messages can be ordered/paginated
- no heap
- no text-based classification
- no color/style change
- T026/T027 regressions remain green
- Linux CTest passes
- portable unit suite passes
- architecture checks pass
- real ADV build passes
- git diff --check passes
- no unrelated cleanup

## Automated tests

Run:

git status --short
cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure

cmake -S tests/unit -B /tmp/T028-build-unit
cmake --build /tmp/T028-build-unit -j"$(nproc)"
ctest --test-dir /tmp/T028-build-unit --output-on-failure

PYTHONDONTWRITEBYTECODE=1 python3 tests/app_dependency_boundary.py . ft8
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_platform_boundary.py . ft8
PYTHONDONTWRITEBYTECODE=1 python3 tests/ft8_platform_boundary.py .
PYTHONDONTWRITEBYTECODE=1 python3 tests/architecture_rules.py .

source ~/projects/esp-idf/export.sh
idf.py -C platform/adv build

git diff --check

Focused sanitizer coverage for the new pure module/controller regression is
recommended if practical.

No GitHub Actions wait.

## Manual validation

No RF requirement.

After supervisor review, visually validate on live Linux/QMX:
- reply-to-me block strongest to weakest
- CQ block strongest to weakest
- regular block strongest to weakest
- selecting a displayed CQ replies to that exact station

Color coding is intentionally deferred.

## Branch workflow

Use codex/T028-rx-message-order.

Codex:
1. read this packet and pinned V2 comparator
2. add the private controller ordering module
3. keep factual batch immutable
4. integrate display and manual selection through one shared map
5. add focused and integration regressions
6. run all local gates
7. set Status to REVIEW
8. record files/results
9. push one reviewable implementation commit
10. return exact SHA
11. no PR
12. no Actions wait

## Codex implementation notes

### Implementation summary

### Files changed

### Invariants preserved

### Local tests run

### Manual/hardware validation still required

### Known limitations / risks

### Commit

## Supervisor review

Review exact task-head to implementation diff.

## Architect test result

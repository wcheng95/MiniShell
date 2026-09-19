# T028 — RX message ordering for display and selection

Status: TESTING

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

Added private pure `app_rx_order` index insertion sort. Its only comparison keys
are to-me/CQ/regular group, descending `snr_db`, and ascending original index.
The pinned V2 `main/main.cpp:dec_sort_cmp` was inspected at the specified commit;
T028 deliberately extends SNR ordering from CQ to all three groups.

`AppRxState` owns a fixed 50-index map, count and generation. Completed batches
publish their map together with the new generation. Model projection and manual
selection use that same map; `selected_rx_index` remains a factual batch index.
Stream reset, audio discontinuity and TX-related RX reset invalidate the map and
selection. A discontinuity that clears visible rows reports a model change.
Retained factual batch/history and automatic-processing freshness are preserved.

### Files changed

- `apps/ft8/src/app_controller/app_rx_order.[ch]`: pure bounded index ordering.
- `apps/ft8/src/app_controller/app_controller.c`: map storage, completed-batch
  lifecycle, reset invalidation, display projection and selection translation.
- `CMakeLists.txt`, `platform/adv/main/CMakeLists.txt`: production composition and
  focused Linux test registration.
- `tests/ft8_rx_order_test.c`: all requested pure sorting cases, full capacity,
  immutable input, explicit ties and conflicting non-key metadata.
- `tests/ft8_physical_tx_test.c`: production builder/controller integration,
  selection, automatic-processing/log ordering, generation/reset and 50-row
  coverage. T026/T027 fixtures now publish completed batches through the same
  private helper as production.
- `tests/ft8_rx_discontinuity_test.c`: visible-map invalidation on gaps while
  preserving factual history/hash state, then rebuilding at next completion.
- This task packet: implementation and test handoff.

### Invariants preserved

No factual RxMessage reordering or mutation. `app_process_addressed_batch` is
unchanged. Integration asserts its AutoSeq result equals processing the original
batch sequentially, all six RT RX records remain in raw order, and processing the
same generation twice does not duplicate records. Exact mixed display map is
`[4,2,3,1,5,0]`; the weaker addressed entry remains ahead of stronger CQs.

The weak-A/strong-B regression selects displayed index 0, verifies factual index
1 and AutoSeq DX `W1DDD` (strong B). Additional coverage projects all 50 rows,
selects global index 49, rejects invalid/stale indexes, and clears empty/reset
maps. Existing UI absolute-index pagination, Linux/ADV presentation, physical TX,
T026 RR73 and T027 non-standard path regressions pass.

No new heap allocations, API/config/log format changes, text-based classification,
UI styling, AutoSeq policy, candidate/SNR computation or protocol changes. Fixed
map storage adds 50 `size_t` entries plus count and generation to existing RX
state. No deviation from task scope.

### Local tests run

```bash
git status --short
cmake -S . -B build-linux
cmake --build build-linux -j8
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure
# PASS 59/59
cmake -S tests/unit -B /tmp/T028-build-unit
cmake --build /tmp/T028-build-unit -j8
ctest --test-dir /tmp/T028-build-unit --output-on-failure
# PASS 15/15
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_dependency_boundary.py . ft8
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_platform_boundary.py . ft8
PYTHONDONTWRITEBYTECODE=1 python3 tests/ft8_platform_boundary.py .
PYTHONDONTWRITEBYTECODE=1 python3 tests/architecture_rules.py .
# All PASS
cmake -S . -B /tmp/T028-build-sanitize \
  -DCMAKE_C_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
  -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined'
cmake --build /tmp/T028-build-sanitize --target ft8_rx_order_unit ft8_physical_tx_unit ft8_rx_discontinuity_unit -j8
ctest --test-dir /tmp/T028-build-sanitize --output-on-failure -R 'ft8_rx_order|ft8_physical_tx|ft8_rx_discontinuity'
# PASS 3/3; run outside sandbox for LeakSanitizer/ptrace compatibility
source ~/projects/esp-idf/export.sh
idf.py -C platform/adv build
# PASS real ESP32-S3 build
git diff --check
# PASS
```

An intermediate Linux suite run hit the existing intermittent `linux_serial_unit`
timeout assertion (also observed during T027). Final complete rerun passed 59/59;
no serial implementation or test was changed. Existing untracked Python caches
were left alone. No GitHub Actions wait.

### Manual/hardware validation still required

No RF or hardware testing performed. After supervisor review, architect should
visually confirm live Linux/QMX group order, descending strength within each group,
and that selecting a displayed CQ replies to that station. Color coding remains
deferred.

### Known limitations / risks

Ordering is a per-completed-batch snapshot; no intermediate dynamic re-sort.
Stream resets clear visible/selectable rows until a new complete batch, while
retaining factual batch history for existing processing/freshness semantics.
Insertion sort is O(50 squared), uses fixed storage and allocates no heap.

### Commit

One implementation commit on `codex/T028-rx-message-order`, based on task head
`47768ad`. The commit containing these notes is the implementation reference;
exact SHA is returned in the handoff. No PR.

## Supervisor review

PASS on implementation commit `bb3e74bfcf4ef6bcff9ab3ae7277f825bae19fa8`.

Reviewed the exact single implementation commit from task head
`47768adcb7f9b41b4e02f999fc660e4af35d7f48`. No blocking finding.

Accepted ownership and data flow:

```text
RxBatch factual order
    |-- app_process_addressed_batch -> RT log / AutoSeq in raw order
    |
    `-- app_rx_order -> display_order[] index map
                         |-- UiModel RX rows
                         `-- manual selection -> factual batch index
```

The new `app_rx_order` module is correctly private to `app_controller`, pure,
fixed-size, heap-free, and uses only the approved factual keys:

```text
primary    is_to_me -> is_cq -> regular
secondary  snr_db descending
tertiary   original index ascending
```

This deliberately preserves the pinned V2 group precedence while extending
strongest-to-weakest ordering to all three groups per architect direction.
`is_to_me` wins when both flags are true.

Accepted lifecycle behavior:

- a completed batch publishes a new order map with the same generation;
- UI rendering uses only a map matching the current batch generation;
- manual selection maps the displayed/global index back to the original factual
  index before creating the AutoSeq event;
- stream reset, audio discontinuity, TX pause and RX resume invalidate visible
  selection/order state;
- factual batch/history is not reordered or destroyed merely to clear display
  state;
- a new completed batch rebuilds the full mapping;
- pagination remains based on all up-to-50 ordered rows.

Accepted regressions:

- exact mixed map `[4,2,3,1,5,0]`;
- descending SNR in reply-to-me, CQ and regular groups;
- explicit stable equal-SNR ordering by original index;
- full 50-entry bounded projection;
- immutable input array;
- displayed strong CQ selection maps to the correct raw message/DX;
- weaker reply-to-me remains ahead of stronger CQ;
- raw automatic-processing result matches sequential raw-batch processing;
- RT RX records remain in raw batch order and are not duplicated by display
  sorting;
- T026/T027 physical/protocol regressions remain green;
- discontinuity invalidates visible mapping and rebuilds after the next completed
  window.

The implementation does not move policy into `ui_shell`, does not parse
canonical text for grouping, and introduces no color/style change, decoder
reordering, AutoSeq queue change, SNR computation change, or public API change.

Accepted local evidence:

```text
Linux CTest          59/59 PASS
portable units       15/15 PASS
focused ASan/UBSan   3/3 PASS
architecture checks  PASS
real ADV build       PASS
git diff --check     PASS
```

The reported intermittent `linux_serial_unit` timeout is non-blocking: the final
complete suite passed and T028 does not touch Serial code.

T028 is TESTING. Manual acceptance is visual/live only: confirm reply-to-me, CQ,
regular group order; descending strength within each group; and that selecting a
displayed CQ replies to that exact station. No RF transmission is required.


## Architect test result

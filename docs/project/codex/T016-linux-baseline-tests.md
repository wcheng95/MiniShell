# T016 — Repair stale Linux regression expectations

Status: READY

## Objective

Fix the two long-standing Linux CTest failures without changing production behavior:

```text
linux_audio
linux_ft8
```

These have been accepted as stale expectations since T001. T016 is test-only.

Do not change production source merely to make these tests green.

## Baseline evidence

### linux_audio

Current `audio_probe` prints:

```text
audio_probe: PASS frames=<n> rate=<...>Hz peak=<L>/<R> mean_abs=<L>/<R> unequal_lr=<n> hash=<hex>
```

The test still expects the obsolete contiguous substring:

```text
audio_probe: PASS frames=<n> hash=<hex>
```

The frames/hash are still correct; diagnostic fields were inserted between them.

### linux_ft8

The test's AS-5 rotation section waits specifically for:

```text
N5CH     RPLY 0/3
```

to appear on rotated page 1.

Current deterministic queue order has `N5CH` and `KQ4PUG` in the opposite relative
order from that stale assumption. Earlier assertions intentionally checked only the
set of page-2 calls, not their order, so the stale ordering assumption first becomes
visible at the rotation wait.

The production primitive is:

```c
auto_seq_rotate_same_parity()
```

which moves the queue head to the end of the contiguous same-parity run. The test
should verify that semantic transform from the observed pre-rotation queue order,
not impose an unrelated hard-coded ordering between two same-priority CQs.

## Mandatory first step

Before editing, reproduce only the two failures and save/report the exact output:

```bash
cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"

ctest --test-dir build-linux -R '^(linux_audio|linux_ft8)$' --output-on-failure
```

Confirm they match the stale-expectation diagnoses above. If either failure has
changed materially, stop and report rather than editing around a new product bug.

## 1. Repair linux_audio robustly

Update `tests/linux_audio.py`.

Do not merely loosen the assertion to `"audio_probe: PASS" in output`.

Instead parse the two `audio_probe: PASS ...` lines and independently derive the
expected deterministic fixture metrics from the WAV payload.

At minimum validate for each of the two runs:

```text
frames
hash
peak L/R
mean_abs L/R
unequal_lr
```

For file/WAV playback mode the probe does not measure a live elapsed-time rate, so
the current expected rate field is `0.000Hz`. Validate it if appropriate to the
current probe contract.

Recommended test shape:

1. decode fixture PCM as little-endian signed 16-bit stereo;
2. compute:
   - frame count;
   - FNV-1a hash exactly as production probe does;
   - per-channel absolute peak, including INT16_MIN behavior matching the probe;
   - integer mean absolute value;
   - count of frames where L != R;
3. parse output with an explicit regex/named groups;
4. require exactly two PASS records;
5. compare every parsed deterministic field with independently computed values;
6. continue rejecting any `audio_probe: FAIL`.

This should strengthen the test while decoupling it from incidental field adjacency.

Do not change `tests/apps/audio_probe.c` unless reproduction proves the probe itself
is wrong.

## 2. Repair linux_ft8 around semantic queue order

Update `tests/linux_ft8.py`.

### Preserve AS-3 validation

The deterministic fixture must still produce the expected **set** of eight factual
CQ contexts:

```text
N4NJJ
AG6X
AE7KJ
W7RPS
N7REB
WN0KS
N5CH
KQ4PUG
```

Keep:

- 8 active RPLY contexts;
- 2 TX pages before rotation;
- page sizes 6 + 2;
- state/retry text expectations;
- no production ordering change.

### Capture actual pre-rotation order

From rendered T page 1 and page 2, parse the row order into one eight-call list.

Assert:

- length = 8;
- no duplicates;
- set equals the expected eight-call set;
- all displayed rows are `RPLY 0/3`.

Do not encode an unsupported relative ordering between `N5CH` and `KQ4PUG`.

### Assert the AS-5 rotation invariant

All eight fixture CQs have the same TX parity in this deterministic `--rx-slot`
scenario.

After returning to page 1 and pressing Enter, parse the two pages again (or enough
rendered state to reconstruct the whole queue) and assert:

```python
rotated_order == initial_order[1:] + initial_order[:1]
```

This directly tests `auto_seq_rotate_same_parity()` semantics.

Also assert:

- still 8 active rows;
- still pages 6 + 2;
- the old head moved to the final queue position;
- no row was lost/duplicated;
- state/retry remains `RPLY 0/3`.

Avoid waiting for one specific call that may live on either page depending on the
pre-rotation order. Wait for a stable frame/page marker and then parse the screen.

### Preserve drop/page-collapse coverage

After rotation:

1. drop visible line 1;
2. assert exactly the rotated head is gone;
3. assert the remaining order equals `rotated_order[1:]`;
4. page 2 must contain the single final row;
5. drop that page-local row;
6. assert paging collapses to 1/1;
7. assert remaining order/set is exactly the expected six contexts after those two
   dynamic removals.

Prefer deriving these expectations from the captured lists rather than hard-coding
call positions.

This retains and improves the existing absolute-index/page-local action regression.

## Parsing guidance

It is acceptable to add small test-local helpers such as:

```text
current_screen(...)
parse_tx_rows(...)
collect_tx_pages(...)
```

Keep them inside `tests/linux_ft8.py`.

The parser should operate on the actual rendered terminal output and fail clearly if
the expected row format changes.

Do not alter UI rendering or AutoSeq ordering.

## Optional focused AutoSeq unit assertion

If useful, add a tiny pure AutoSeq test proving a same-parity queue rotation preserves
relative tail order and moves only the head to the end.

Only do this if it adds meaningful coverage without duplicating an existing unit.
No production source change.

## Scope

Expected changes:

```text
tests/linux_audio.py
tests/linux_ft8.py
docs/project/codex/T016-linux-baseline-tests.md
```

Possibly one small test-only helper/unit registration if justified.

Do not change:

```text
tests/apps/audio_probe.c
apps/ft8/
core/
platform/
public API
fixture WAV content
reference outputs
```

unless reproduction demonstrates a real product defect, in which case mark BLOCKED
and report instead.

## Local test gate

Run:

```bash
git status --short

cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"

ctest --test-dir build-linux -R '^(linux_audio|linux_ft8)$' --output-on-failure

python3 tests/app_dependency_boundary.py . ft8
python3 tests/app_platform_boundary.py . ft8
python3 tests/ft8_platform_boundary.py .

cmake -S tests/unit -B /tmp/T016-build-unit
cmake --build /tmp/T016-build-unit -j"$(nproc)"
ctest --test-dir /tmp/T016-build-unit --output-on-failure

git diff --check

ctest --test-dir build-linux --output-on-failure
```

Target outcome:

```text
full root Linux CTest: 37/37 PASS
unit suite:            14/14 PASS
```

Use the actual test count if CTest discovery changes, but there should be **zero**
known/accepted failures after T016.

No ADV build/hardware validation is required because production code is unchanged.

## Acceptance criteria

- [ ] both failures reproduced before edits and match stale expectations;
- [ ] linux_audio independently validates current deterministic probe metrics;
- [ ] linux_audio no longer depends on frames/hash adjacency;
- [ ] exactly two successful probe runs are still required;
- [ ] linux_ft8 preserves expected eight-call CQ set;
- [ ] linux_ft8 captures real queue order before rotation;
- [ ] rotation is asserted as `initial[1:] + initial[:1]`;
- [ ] no unsupported N5CH/KQ4PUG relative order is hard-coded;
- [ ] drop/page-local absolute-index behavior remains covered;
- [ ] page collapse remains covered;
- [ ] no production source changed;
- [ ] architecture checks pass;
- [ ] unit suite passes;
- [ ] full Linux CTest is green;
- [ ] no expectations are weakened to generic PASS-only checks;
- [ ] no unrelated cleanup.

## Branch workflow

Use:

```text
codex/T016-linux-baseline-tests
```

Before handoff:

1. record exact pre-edit failure output/diagnosis;
2. implement test-only changes;
3. set Status to REVIEW;
4. run focused and full local suites;
5. commit and push;
6. return commit SHA;
7. no PR;
8. no Actions wait.

Supervisor reviews `main..<SHA>`. If clean and full Linux CTest is green, merge
directly; no hardware TESTING phase is needed.

## Codex implementation notes

### Pre-edit failure reproduction

### linux_audio repair

### linux_ft8 repair

### Files changed

### Local tests/results

### Behavior/invariants preserved

### Known limitations / risks

### Commit

## Supervisor review

Supervisor reviews that both changes repair stale assertions rather than mask product bugs.

## Architect test result

No hardware validation required for this test-only task.

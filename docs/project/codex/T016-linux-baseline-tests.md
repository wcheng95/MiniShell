# T016 — Repair stale Linux regression expectations

Status: COMPLETE

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

- [x] both failures reproduced before edits and match stale expectations;
- [x] linux_audio independently validates current deterministic probe metrics;
- [x] linux_audio no longer depends on frames/hash adjacency;
- [x] exactly two successful probe runs are still required;
- [x] linux_ft8 preserves expected eight-call CQ set;
- [x] linux_ft8 captures real queue order before rotation;
- [x] rotation is asserted as `initial[1:] + initial[:1]`;
- [x] no unsupported N5CH/KQ4PUG relative order is hard-coded;
- [x] drop/page-local absolute-index behavior remains covered;
- [x] page collapse remains covered;
- [x] no production source changed;
- [x] architecture checks pass;
- [x] unit suite passes;
- [x] full Linux CTest is green;
- [x] no expectations are weakened to generic PASS-only checks;
- [x] no unrelated cleanup.

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

Before any edits, configured/built Linux and ran:

```bash
ctest --test-dir build-linux -R '^(linux_audio|linux_ft8)$' --output-on-failure
```

Result: **0/2 PASS**, exit 8. Full output was saved to `/tmp/T016-before.log`
and is reproduced below (ESC bytes escaped as `\x1b` for Markdown readability).
Both failures match the task diagnosis. Audio reports correct frames/hash with
intervening diagnostics. FT8 initially renders queue order
`N4NJJ, AG6X, W7RPS, AE7KJ, N7REB, WN0KS, KQ4PUG, N5CH`; rotation correctly makes
`AG6X, W7RPS, AE7KJ, N7REB, WN0KS, KQ4PUG` page 1, so waiting for N5CH there times
out. Inspected `auto_seq_rotate_same_parity()` to confirm head-to-tail semantics.
No new production defect was encountered.

<details>
<summary>Exact pre-edit CTest output (terminal ESC bytes escaped)</summary>

```text
Internal ctest changing into directory: /home/wei/projects/MiniShell/build-linux
Test project /home/wei/projects/MiniShell/build-linux
    Start  9: linux_audio
1/2 Test  #9: linux_audio ......................***Failed    0.08 sec
minishell
M$> audio_probe: PASS frames=180140 rate=0.000Hz peak=5439/5436 mean_abs=1173/1173 unequal_lr=155431 hash=f05f17c990b748e1
M$> audio_probe: PASS frames=180140 rate=0.000Hz peak=5439/5436 mean_abs=1173/1173 unequal_lr=155431 hash=f05f17c990b748e1
M$> expected twice: audio_probe: PASS frames=180140 hash=f05f17c990b748e1

    Start 29: linux_ft8
2/2 Test #29: linux_ft8 ........................***Failed    3.13 sec
minishell
M$> ft8
\x1b[2J\x1b[H\x1b[1;1HFT8  20m  Default    RX       \x1b[2;1H                              \x1b[3;1H                              \x1b[4;1H                              \x1b[5;1H                              \x1b[6;1H                              \x1b[7;1H                              \x1b[8;1HR T O S V  1-6 select q quit  \x1b[2J\x1b[H\x1b[1;1HFT8  20m  Default    O        \x1b[2;1H>1 Protocol: FT8              \x1b[3;1H 2 Profile: Default           \x1b[4;1H 3 Band: 20m                  \x1b[5;1H 4 CQ / Beacon >              \x1b[6;1H 5 TX >                       \x1b[7;1H 6 Message >                  \x1b[8;1H2-6 Enter <>chg `back q quit  \x1b[2J\x1b[H\x1b[1;1HFT8  20m  Default    O        \x1b[2;1H>1 Offset Source: --          \x1b[3;1H 2 Fixed Offset: --           \x1b[4;1H 3 Skip TX1: OFF              \x1b[5;1H 4 Max Retry: 3               \x1b[6;1H 5 Tune: --                   \x1b[7;1H                              \x1b[8;1H<> changes wired items `back q\x1b[2J\x1b[H\x1b[1;1HFT8  20m  Default    O        \x1b[2;1H 1 Offset Source: --          \x1b[3;1H 2 Fixed Offset: --           \x1b[4;1H>3 Skip TX1: ON               \x1b[5;1H 4 Max Retry: 3               \x1b[6;1H 5 Tune: --                   \x1b[7;1H                              \x1b[8;1H<> changes wired items `back q\x1b[2J\x1b[HM$> ft8 --profile desktop
\x1b[2J\x1b[H\x1b[1;1HFT8  20m  Default    RX       \x1b[2;1H                              \x1b[3;1H                              \x1b[4;1H                              \x1b[5;1H                              \x1b[6;1H                              \x1b[7;1H                              \x1b[8;1HR T O S V  1-6 select q quit  \x1b[2J\x1b[H\x1b[1;1HFT8  20m  Default    V        \x1b[2;1H>1 Memory >                   \x1b[3;1H 2 GPS >                      \x1b[4;1H 3 QSO / Log >                \x1b[5;1H 4 Performance >              \x1b[6;1H 5 System Info >              \x1b[7;1H 6 About >                    \x1b[8;1H1-6 Enter  read only  q quit  \x1b[2J\x1b[H\x1b[1;1HFT8  20m  Default    V        \x1b[2;1HHeap free: 7.9M               \x1b[3;1HLargest: --                   \x1b[4;1HApp alloc: 2.5K               \x1b[5;1HAlloc count: 1                \x1b[6;1HLargest/free: --              \x1b[7;1HRX: OFF                       \x1b[8;1Hread only        `back q quit \x1b[2J\x1b[H\x1b[1;1HFT8  20m  Default    V        \x1b[2;1H>1 Memory >                   \x1b[3;1H 2 GPS >                      \x1b[4;1H 3 QSO / Log >                \x1b[5;1H 4 Performance >              \x1b[6;1H 5 System Info >              \x1b[7;1H 6 About >                    \x1b[8;1H1-6 Enter  read only  q quit  \x1b[2J\x1b[H\x1b[1;1HFT8  20m  Default    V        \x1b[2;1HRuntime: MiniShell            \x1b[3;1HPresentation: DESKTOP         \x1b[4;1HUI: text 30x8                 \x1b[5;1HApp: ft8                      \x1b[6;1HStation: Default              \x1b[7;1HBand: 20m                     \x1b[8;1Hread only        `back q quit \x1b[2J\x1b[HM$> ft8 --profile adv
\x1b[2J\x1b[H\x1b[1;1HRX 20 06:34:36 1/1 6\x1b[2;1H                    \x1b[3;1H                    \x1b[4;1H                    \x1b[5;1H                    \x1b[6;1H                    \x1b[7;1H                    \x1b[2J\x1b[H\x1b[1;1HO  20 06:34:36 1/1 6\x1b[2;1H>1 Protocol: FT8    \x1b[3;1H 2 Profile: Default \x1b[4;1H 3 Band: 20m        \x1b[5;1H 4 CQ / Beacon >    \x1b[6;1H 5 TX >             \x1b[7;1H 6 Message >        \x1b[2J\x1b[H\x1b[1;1HO  20 06:34:36 1/1 6\x1b[2;1H>1 Offset Source: --\x1b[3;1H 2 Fixed Offset: -- \x1b[4;1H 3 Skip TX1: ON     \x1b[5;1H 4 Max Retry: 3     \x1b[6;1H 5 Tune: --         \x1b[7;1H                    \x1b[2J\x1b[H\x1b[1;1HV  20 06:34:36 1/1 6\x1b[2;1H>1 Memory >         \x1b[3;1H 2 GPS >            \x1b[4;1H 3 QSO / Log >      \x1b[5;1H 4 Performance >    \x1b[6;1H 5 System Info >    \x1b[7;1H 6 About >          \x1b[2J\x1b[H\x1b[1;1HV  20 06:34:36 1/1 6\x1b[2;1HRuntime: MiniShell  \x1b[3;1HPresentation: ADV   \x1b[4;1HUI: text 20x7       \x1b[5;1HApp: ft8            \x1b[6;1HStation: Default    \x1b[7;1HBand: 20m           \x1b[2J\x1b[HM$> ft8 --profile adv --rx /flash/kfs.wav --rx-slot 12345
\x1b[2J\x1b[H\x1b[1;1HRX 20 06:34:36 1/1 6\x1b[2;1H                    \x1b[3;1H                    \x1b[4;1H                    \x1b[5;1H                    \x1b[6;1H                    \x1b[7;1H                    \x1b[2J\x1b[H\x1b[1;1HRX 20 06:34:36 1/3 6\x1b[2;1H1 CQ N4NJJ DM26     \x1b[3;1H2 CQ AG6X DM12      \x1b[4;1H3 CQ W7RPS CN85     \x1b[5;1H4 CQ AE7KJ CN86     \x1b[6;1H5 S58MU N0GZ EN31   \x1b[7;1H6 KA3FMO KO6BPG DM12\x1b[2J\x1b[H\x1b[1;1HRX 20 06:34:36 2/3 6\x1b[2;1H1 N6ACA KX0S R-06   \x1b[3;1H2 WM0L K3QM R-06    \x1b[4;1H3 PD0TV N2NT 73     \x1b[5;1H4 CQ N7REB CN74     \x1b[6;1H5 WM0L KA2EEU EM20  \x1b[7;1H6 W1AW/0 <...> 73   \x1b[2J\x1b[H\x1b[1;1HRX 20 06:34:36 3/3 6\x1b[2;1H1 CQ WN0KS EM19     \x1b[3;1H2 WV7Z KE9I EN62    \x1b[4;1H3 CQ KQ4PUG FM16    \x1b[5;1H4 CQ N5CH EM05      \x1b[6;1H                    \x1b[7;1H                    \x1b[2J\x1b[H\x1b[1;1HTX 20 06:34:36 1/2 6\x1b[2;1H1 N4NJJ    RPLY 0/3 \x1b[3;1H2 AG6X     RPLY 0/3 \x1b[4;1H3 W7RPS    RPLY 0/3 \x1b[5;1H4 AE7KJ    RPLY 0/3 \x1b[6;1H5 N7REB    RPLY 0/3 \x1b[7;1H6 WN0KS    RPLY 0/3 \x1b[2J\x1b[H\x1b[1;1HTX 20 06:34:36 2/2 6\x1b[2;1H1 KQ4PUG   RPLY 0/3 \x1b[3;1H2 N5CH     RPLY 0/3 \x1b[4;1H                    \x1b[5;1H                    \x1b[6;1H                    \x1b[7;1H                    \x1b[2J\x1b[H\x1b[1;1HTX 20 06:34:36 1/2 6\x1b[2;1H1 N4NJJ    RPLY 0/3 \x1b[3;1H2 AG6X     RPLY 0/3 \x1b[4;1H3 W7RPS    RPLY 0/3 \x1b[5;1H4 AE7KJ    RPLY 0/3 \x1b[6;1H5 N7REB    RPLY 0/3 \x1b[7;1H6 WN0KS    RPLY 0/3 Traceback (most recent call last):
  File "/home/wei/projects/MiniShell/tests/linux_ft8.py", line 272, in <module>
    raise SystemExit(main())
                     ^^^^^^
  File "/home/wei/projects/MiniShell/tests/linux_ft8.py", line 207, in main
    rotated = read_until(master_fd, b"N5CH     RPLY 0/3", 3.0)
              ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
  File "/home/wei/projects/MiniShell/tests/linux_ft8.py", line 20, in read_until
    raise TimeoutError(f"timed out waiting for {needle!r}; got {bytes(data)!r}")
TimeoutError: timed out waiting for b'N5CH     RPLY 0/3'; got b'\x1b[2J\x1b[H\x1b[1;1HTX 20 06:34:36 1/2 6\x1b[2;1H1 AG6X     RPLY 0/3 \x1b[3;1H2 W7RPS    RPLY 0/3 \x1b[4;1H3 AE7KJ    RPLY 0/3 \x1b[5;1H4 N7REB    RPLY 0/3 \x1b[6;1H5 WN0KS    RPLY 0/3 \x1b[7;1H6 KQ4PUG   RPLY 0/3 \x1b[2J\x1b[H\x1b[1;1HTX 20 06:34:37 1/2 7\x1b[2;1H1 AG6X     RPLY 0/3 \x1b[3;1H2 W7RPS    RPLY 0/3 \x1b[4;1H3 AE7KJ    RPLY 0/3 \x1b[5;1H4 N7REB    RPLY 0/3 \x1b[6;1H5 WN0KS    RPLY 0/3 \x1b[7;1H6 KQ4PUG   RPLY 0/3 \x1b[2J\x1b[H\x1b[1;1HTX 20 06:34:38 1/2 8\x1b[2;1H1 AG6X     RPLY 0/3 \x1b[3;1H2 W7RPS    RPLY 0/3 \x1b[4;1H3 AE7KJ    RPLY 0/3 \x1b[5;1H4 N7REB    RPLY 0/3 \x1b[6;1H5 WN0KS    RPLY 0/3 \x1b[7;1H6 KQ4PUG   RPLY 0/3 \x1b[2J\x1b[H\x1b[1;1HTX 20 06:34:39 1/2 9\x1b[2;1H1 AG6X     RPLY 0/3 \x1b[3;1H2 W7RPS    RPLY 0/3 \x1b[4;1H3 AE7KJ    RPLY 0/3 \x1b[5;1H4 N7REB    RPLY 0/3 \x1b[6;1H5 WN0KS    RPLY 0/3 \x1b[7;1H6 KQ4PUG   RPLY 0/3 '


0% tests passed, 2 tests failed out of 2

Total Test time (real) =   3.21 sec

The following tests FAILED:
	  9 - linux_audio (Failed)
	 29 - linux_ft8 (Failed)
Errors while running CTest
```

</details>

### linux_audio repair

Decode WAV payload independently with little-endian signed stereo samples.
Compute frame count, FNV-1a over payload bytes, saturated absolute peaks and
integer means, and unequal-channel frame count. Saturate abs(-32768) to 32767,
matching the inspected probe contract. Validate non-live rate as `0.000Hz`.

Parse explicit named fields from exactly two PASS records and compare all fields;
malformed records, extra/missing PASS records, nonzero process exit, or any FAIL
remain failures. Actual expected fixture metrics:

```text
frames=180140 rate=0.000Hz peak=5439/5436
mean_abs=1173/1173 unequal_lr=155431 hash=f05f17c990b748e1
```

These values are derived at runtime, not hard-coded as acceptance constants.
The probe and fixture bytes are unchanged.

### linux_ft8 repair

Read complete ADV terminal frames (clear plus seven 20-column row writes), rather
than assuming a PTY read containing the header also contains every row. Validate
page markers, contiguous page-local row numbers, exact `RPLY 0/3` state/retry text,
row counts, and uniqueness. Ignore an already-in-flight clock redraw of the
pre-action rows while waiting for the changed frame; unchanged behavior times out.

Capture initial pages 6+2 and require exactly the eight specified factual CQs.
Capture both rotated pages and compare the entire order with
`initial_order[1:] + initial_order[:1]`. This preserves every row and its relative
tail order and puts the old head last. Reconstruct pages 6+1 after dropping the
rotated head and compare with `rotated_order[1:]`; drop page-local line 1 on page 2
and require page 1/1 with exactly `rotated_order[1:-1]`. No N5CH/KQ4PUG order is
assumed. Existing config persistence, Memory/System Info, profile, RX selection,
clean quit and process-exit checks remain.

### Files changed

- `tests/linux_audio.py`: independently calculated deterministic metric assertions.
- `tests/linux_ft8.py`: complete-frame parsing and semantic queue/action assertions.
- This task packet: reproduction, REVIEW status and test evidence.

### Local tests/results

```bash
git status --short
cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"
ctest --test-dir build-linux -R '^(linux_audio|linux_ft8)$' --output-on-failure
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_dependency_boundary.py . ft8
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_platform_boundary.py . ft8
PYTHONDONTWRITEBYTECODE=1 python3 tests/ft8_platform_boundary.py .
cmake -S tests/unit -B /tmp/T016-build-unit
cmake --build /tmp/T016-build-unit -j"$(nproc)"
ctest --test-dir /tmp/T016-build-unit --output-on-failure
git diff --check
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure
```

Linux configure/build PASS. Before edits the focused run failed as captured above;
after edits **2/2 PASS**. All three standalone architecture commands PASS.
Unit configure/build and **14/14 PASS**. Full root Linux CTest **37/37 PASS**,
including architecture checker self-tests and reference-pin check. Zero accepted
failures remain. Final `git diff --check` PASS.

### Behavior/invariants preserved

Test-only implementation; production, public API, fixture WAV, probe source,
AutoSeq order, UI rendering/actions, DSP and decode expectations are unchanged.
The assertions are stronger than the former substring/set checks. No new build
registration or duplicated pure rotation unit test was needed. No task deviations.

### Known limitations / risks

The terminal parser deliberately follows the current ADV 20x7 frame contract;
rendering-format changes will require a reviewed test update. PTY checks retain
the existing three-second deadlines. No ADV build or hardware validation is
required because production code is unchanged. No PR or Actions wait performed.

### Commit

One test-only implementation commit on `codex/T016-linux-baseline-tests`, titled
`T016: repair Linux audio metrics and FT8 queue assertions`. The pushed SHA is
returned in the handoff; these notes are included in that commit.

## Supervisor review

PASS. Reviewed `400f1c644178b9af043f1199baabb93e47ec34b6` against `main`.

The changes are test-only. `linux_audio` now parses exactly two complete PASS records and independently derives frame count, saturated per-channel peaks, integer mean absolute values, unequal-channel count, non-live rate, and FNV-1a hash from the WAV fixture. This removes the brittle field-adjacency assumption while increasing coverage.

`linux_ft8` now reconstructs the actual rendered TX queue across both ADV pages, requires the expected eight-call set with exact `RPLY 0/3` rows, verifies AS-5 rotation as `initial[1:] + initial[:1]`, then derives the drop/page-collapse expectations from that captured order. The stale N5CH/KQ4PUG relative-order assumption is gone without weakening queue semantics.

Focused tests 2/2, architecture checks, unit suite 14/14, and full Linux CTest 37/37 pass. No production source, fixture, probe, UI, AutoSeq, DSP, or API behavior changed. Commit was fast-forwarded directly to `main`.

## Architect test result

ACCEPTED. No hardware validation required.

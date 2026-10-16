# T087 — Restore V2 ADV decoder scheduling

Status: REVIEW

## Intent

Restore the pinned MiniFT8-V2 decode scheduling urgency on ADV while preserving
V3's separate core-1 decoder worker and MiniShell ownership boundaries.

Current V3 ADV decode worker:

```text
core      1
priority  tskIDLE_PRIORITY + 1
behavior  taskYIELD() after every decode work unit
```

Pinned V2 stream/decode task:

```text
core      1
priority  4
behavior  decode candidates in one tight run; no deliberate per-candidate yield
```

On an active FT8 band, V3 has shown crowded slots with 13+ decoded messages and
decode work extending well into the following slot. T087 restores the V2
scheduling policy without changing decoder mathematics.

Pinned V2 reference:

```text
wcheng95/Mini-FT8
491e757ae6b1e4cfd2b9a6ba10f48b35643849e0
```

## Required behavior

ADV only:

- keep decoder worker pinned to core 1;
- keep the existing static 8192-byte decode stack;
- change decode worker priority to exactly FreeRTOS priority 4;
- when `app_controller_decode_worker_step()` reports `did_work=true`, loop
  immediately to the next decode work unit;
- remove the deliberate `taskYIELD()` from the active decode path;
- when no decode work is available, retain a blocking/delay path so the idle
  worker does not spin;
- retain current stop/abort lifecycle and diagnostics unless a narrow change is
  required by compilation/tests.

Do not change:

- 50-candidate capacity;
- minimum sync score 5;
- strongest-first candidate ordering;
- BP-LDPC implementation;
- 25-iteration maximum;
- hash/message semantics;
- ADV decoder core affinity;
- capture/FFT timing model;
- RxTxLog policy (T086 owns that);
- block normalization;
- speculative/two-pass decoding.

This task changes scheduling only.

## Priority rationale

The intended deadline hierarchy is:

```text
USB/UAC transport acquisition
    highest infrastructure urgency

RX/waterfall production
    highest FT8 application deadline

FT8 decoder
    high compute urgency on otherwise independent core 1

UI / logging / diagnostics
    lower urgency
```

Priority 4 matches the known-good V2 scheduling reference. Higher-priority USB
tasks remain able to preempt it.

## MiniFT8 version

This changes the ADV production MiniFT8 binary.

Update `V -> 6 About` to:

```text
MiniFT8-V3.087
```

Update the matching UI assertion.

## Tests

Add a focused scheduling-policy regression test. It may be a small source-policy
test if compiling FreeRTOS task machinery in the Linux unit harness would add
unnecessary complexity.

The test must prove at minimum:

1. `ADV_FT8_DECODE_CORE == 1`.
2. decode priority is exactly 4.
3. the active `did_work` path contains no `taskYIELD()`.
4. the no-work path remains non-spinning/bounded (for example `vTaskDelay(1)`).
5. static decode stack remains 8192 bytes.
6. MiniFT8 About assertion is exactly `MiniFT8-V3.087`.

Run:

```text
cmake -S . -B build-linux
cmake --build build-linux -j8
ctest --test-dir build-linux --output-on-failure
git diff --check

source /home/wei/projects/esp-idf/export.sh
idf.py -C platform/adv build
```

## Hardware/manual validation

On ADV/QMX:

1. launch `ft8`;
2. confirm startup diagnostic reports core 1, priority 4, 8192-byte stack;
3. operate on an active band;
4. record representative:
   - candidate-search time;
   - total decode time;
   - candidate count;
   - message count;
   - UAC high-water / overflow;
   - any zero-decode slot.

Primary performance expectation: crowded-slot decode time should improve
materially relative to the priority-1/per-work-unit-yield baseline.

T087 is accepted by correct scheduling restoration and normal RX operation; full
elimination of the zero-decode symptom remains the broader repair target.

## Expected files

Likely:

```text
platform/adv/adv_ft8_decode.c
tests/adv_ft8_decode_policy_test.py
CMakeLists.txt
apps/ft8/src/ui_shell/ui_shell.c
tests/ft8_ui_smoke.c
docs/MiniFT8/development.md
docs/project/codex/T087-ft8-v2-decode-scheduling.md
```

Avoid unrelated changes.

## Branch / handoff

Use:

```text
codex/T087-ft8-v2-decode-scheduling
```

Record:

```text
Implementation summary
Files changed
Behavior/invariants preserved
Tests run and results
Hardware/manual validation still required
Known limitations or risks
Commit reference
```

## Codex handoff

### Implementation summary

Restored the pinned V2 scheduling policy specified by this task: the ADV
core-1 decoder runs at exactly FreeRTOS priority 4 and does not deliberately
yield between active work units. The idle branch still calls `vTaskDelay(1)`.
About now displays `MiniFT8-V3.087`. No task deviations.

### Files changed

- `platform/adv/adv_ft8_decode.c`: priority 4 and removal of active `taskYIELD()`.
- `tests/adv_ft8_decode_policy_test.py`: source-policy regression for core,
  exact priority, static stack and task creation wiring, active/idle scheduling,
  and About identity in production and its UI test.
- `CMakeLists.txt`: register the scheduling-policy test with CTest.
- `apps/ft8/src/ui_shell/ui_shell.c`: About version.
- `tests/ft8_ui_smoke.c`: matching About assertion.
- `docs/MiniFT8/development.md`: implemented scheduling policy, pending hardware
  acceptance.
- This task packet: implementation and validation evidence.

### Behavior/invariants preserved

Core 1, static 8192-byte stack, stop/abort lifecycle, and startup/stack diagnostics
are unchanged. No decoder mathematics, 50-candidate capacity, minimum sync score
5, strongest-first ordering, BP-LDPC or 25-iteration limit changes. Hash/message
semantics, capture/FFT timing, T086 logging policy, and MiniShell ownership
boundaries remain unchanged. No normalization or speculative decode was added.

### Tests run and results

- `python3 tests/adv_ft8_decode_policy_test.py`: 3/3 passed.
- `cmake -S . -B build-linux`: passed.
- `cmake --build build-linux -j8`: passed.
- `ctest --test-dir build-linux --output-on-failure`: 130/130 passed
  (59.31 seconds).
- `git diff --check`: passed.
- `source /home/wei/projects/esp-idf/export.sh` then
  `idf.py -C platform/adv build`: passed. ESP-IDF headers emitted
  `#include_next is a GCC extension` warnings.

### Hardware/manual validation still required

On ADV/QMX, confirm startup reports core 1, priority 4, stack 8192 bytes and
About reads `MiniFT8-V3.087`. Confirm normal active-band RX operation and record
candidate-search time, total decode time, candidate/message counts, UAC
high-water/overflow, and zero-decode slots. Compare crowded-slot timings with the
priority-1/per-work-unit-yield baseline.

### Known limitations or risks

Host source-policy checks establish the configured scheduling policy, not actual
FreeRTOS timing or live-radio performance. ADV/QMX timing and normal RX evidence
remain pending; no claim is made that zero-decode symptoms are eliminated.

### Commit reference

The single implementation commit containing this handoff on
`codex/T087-ft8-v2-decode-scheduling`; full SHA supplied in the chat handoff.

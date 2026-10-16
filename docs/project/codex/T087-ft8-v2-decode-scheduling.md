# T087 — Restore V2 ADV decoder scheduling

Status: READY

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

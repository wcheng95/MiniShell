# T081 — MiniFT8 live RX capture/decode decoupling

Status: COMPLETE

## Architect intent

Fix the remaining MiniFT8-V3 live-RX lifecycle coupling without changing the
FT8 DSP.

The receive design is intentionally simple:

```text
every slot at UTC - 1.60 s
    reset the slot-local waterfall writer to block 0
    start/fill that slot's waterfall

every slot at UTC + 12.64 s
    start decoding that slot

repeat forever while live RX is active
```

Capture and decode are separate paths. Capture may provide a slot snapshot/view
to the decoder, but capture timing must never wait for, inspect, cancel, or be
rescheduled by decoder progress or result publication.

A slot that decodes zero messages is valid. A receive slot that disappears
because lifecycle state prevented its decode trigger is a bug.

## Problem in the current V3 implementation

The current ADV implementation runs capture and decode on separate execution
paths, but their control lifecycles are still coupled.

Current coupling includes:

1. `RX_SLOT_FRAMER_EVENT_FINALIZE_WINDOW` checks
   `decode_async_state != IDLE` and silently returns `skip-busy`. This can
   discard a slot because the previous decoder/result state was not cleared.

2. Decoder execution and result delivery share one state progression:

   ```text
   IDLE -> RUNNING -> RESULT -> IDLE
   ```

   A completed decode remains non-IDLE until the main application consumes and
   publishes the result. Result-publication latency therefore affects the next
   slot's decode eligibility.

3. Live Audio discontinuity handling sets `timing_pending`, requests decoder
   cancellation, and can refuse fresh RX processing/reset work while the
   decoder is RUNNING/CANCEL_REQUESTED.

4. `ft8_engine_reset_stream()` resets both producer-side monitor/timing state
   and decoder-job state, making an RX transport reset inherently a decode
   lifecycle operation.

These are lifecycle/ownership issues. They are not reasons to change candidate
search, LDPC, FFT geometry, or FT8 timing.

## Required live-RX timing contract

UTC remains the authority for every live slot. Do not derive the next slot from
completion of the previous slot.

For slot N:

```text
N UTC - 1.60 s    CAPTURE_RESET
                  reset writer/FFT slot-local state
                  begin filling slot N

N UTC + 12.64 s   DECODE_START(N)
                  submit the current slot-N decode view/job

N+1 UTC - 1.60 s  CAPTURE_RESET
                  happens regardless of decode-N state/result publication

N+1 UTC + 12.64 s DECODE_START(N+1)
                  happens on the normal schedule
```

The next capture reset is never delayed by decoding.

The next decode trigger is never intentionally omitted because a previous job
or result is late. If the decoder/result buffer is unexpectedly unavailable at
the next trigger, that is an invariant violation to diagnose, not supported
backpressure behavior.

## Capture-path invariant

After live RX startup, the capture path owns only:

- current UTC-derived slot scheduling;
- frontend sample conversion;
- slot-local block framing;
- producer-side FFT/waterfall writing;
- the recurring `-1.60 s` capture reset;
- the recurring `+12.64 s` decode submission.

Capture must not:

- read decoder RUNNING/RESULT state to decide whether a slot exists;
- wait for decoder completion;
- wait for result publication;
- cancel decoding because Audio reports a discontinuity;
- re-acquire the FT8 timeline through a decoder-dependent `timing_pending`
  state;
- silently skip a decode opportunity.

Remove `skip-busy` as accepted/normal RX behavior.

## Decode-path invariant

ADV keeps one core-1 decode worker.

The decoder owns:

- candidate search;
- candidate storage;
- noise estimate;
- LDPC / CRC;
- message decoding;
- callsign hash mutation needed by decode;
- one active slot decode job.

Observed ADV decode time is normally much shorter than one FT8 slot and has
been about 4 seconds at the longest in current hardware use. There is therefore
ample time to complete and consume a result before the next `+12.64 s`
decode trigger.

A decoder still RUNNING at the next slot decode trigger is an invariant
failure, not an expected busy-band condition.

## Result ownership: keep one buffer

Do **not** add a second completed-result message buffer, result queue, or deep
copy.

Keep the existing single:

```text
protocol_messages[50]
```

ownership model.

Normal lifecycle:

```text
decode N writes protocol_messages[]
        |
        v
decode N completes
        |
        v
main/controller consumes result N promptly
        |
        v
RxBatch N built/published
        |
        v
protocol_messages[] free long before decode N+1
```

The current `Ft8ProtocolSlot` result may continue to refer to
`protocol_messages[]` while the result is pending.

The implementation may separate execution state from result-ready state if
that simplifies correctness, for example:

```text
decoder execution:
    IDLE / RUNNING

result ownership:
    NONE / READY
```

but do not add storage merely to tolerate an unconsumed result.

Starting the next decode requires the single result storage to have been
consumed. Under correct operation this is always true well before the next
trigger. If it is not true, emit explicit diagnostics and treat it as a bug.

Required diagnostic distinction at a violated next-slot trigger:

```text
decoder still RUNNING
    -> report prior decode slot + elapsed time

decoder IDLE but previous result still READY
    -> report previous result slot + result age
```

Do not collapse either condition into a benign `skip-busy` message.

## Live Audio discontinuity policy

A live transport imperfection damages receive data; it does not redefine FT8
time.

For live RX, remove the current discontinuity lifecycle:

```text
discontinuity
    -> timing_pending
    -> cancel decode
    -> wait for decoder/cancel state
    -> stream reset / timing reacquisition
```

A discontinuity may still reset frontend conversion state if that is required
to keep the channel/decimation implementation sane, and backend/provider code
may pad known missing samples where already supported.

But the FT8 application must not:

- cancel an already-running slot decode because of a later Audio discontinuity;
- block capture while cancellation completes;
- make the next UTC capture reset depend on a reset handshake;
- make the next slot depend on the damaged slot.

The next UTC `-1.60 s` reset is the recovery mechanism. A damaged current slot
may produce fewer messages or zero messages.

Do not redesign QMX device disappearance/re-enumeration in T081. Device
availability remains an Audio/provider/platform concern.

## Ft8Engine ownership cleanup

Keep this task local. Do not rewrite the complete FT8 engine.

Separate producer-side reset semantics from decoder-job semantics enough that a
capture reset does not cancel or clear a decode job.

The intended ownership is:

```text
capture / producer:
    monitor writer
    slot-local FFT/waterfall producer state
    UTC slot anchor metadata

decode worker:
    active decode job
    candidates
    LDPC/CRC/message-decode state
    decode-side hash state

intentional shared input:
    read-only slot waterfall/view supplied to the decode job
```

It is acceptable that the next slot's producer eventually overwrites waterfall
data still referenced by an unusually late decoder. That may reduce decode
yield. It must not affect capture scheduling or the next slot lifecycle.

Review callsign-hash aging as part of this split. Capture/UTC anchoring must not
mutate decoder-owned hash state while core 1 is decoding. Base any required
aging on accepted decode slot progression inside decoder ownership rather than
on producer reset events.

## Zero-message slot behavior

Preserve existing correct behavior:

```text
decode completes with 0 messages
    -> build/publish RxBatch for that slot
    -> batch generation advances
    -> RX display count becomes 0
```

Zero messages and no slot result are different states.

## Physical TX boundary

T081 is an RX lifecycle fix. Preserve the existing physical-QMX TX pause/resume
behavior and AutoSeq/TX policy unless a minimal call-site adaptation is required
by the RX state cleanup.

Do not redesign TX scheduling, CAT, tone generation, or TX/RX recovery in this
task.

## Non-goals

Do not change:

- FT8 `-1.60 s` capture reset timing;
- FT8 `+12.64 s` decode timing;
- 6 kHz engine sample rate;
- 960-sample / 160-ms block geometry;
- ADV `time_osr=2, freq_osr=1`;
- 50-candidate policy;
- candidate score/search mathematics;
- LDPC iteration policy;
- SNR estimation;
- UAC task priority/ring sizing;
- UI layout;
- AutoSeq policy;
- physical TX behavior;
- early/partial result delivery;
- deep decoding;
- retained candidate-local FFT data.

Future early result delivery and deep decoding must build on this decoupled
lifecycle, not be implemented as part of T081.

## Required tests

Add focused regression coverage for the lifecycle, not only DSP output.

### 1. Consecutive-slot schedule

Drive enough live timed samples for several slots and prove, for every slot:

```text
CAPTURE_RESET(N)
DECODE_START(N)
CAPTURE_RESET(N+1)
DECODE_START(N+1)
...
```

No slot may be omitted because of result/publication bookkeeping.

### 2. Decode crossing the next capture reset

Hold a decode job active across the following slot's `-1.60 s`
`CAPTURE_RESET`.

Verify:

- capture reset still occurs;
- producer state starts the new slot;
- the decoder is not canceled solely because producer reset occurred;
- capture does not wait for decode.

This is intentionally different from holding decode through the next
`+12.64 s` trigger, which is an invariant failure.

### 3. Result-ready independence from capture

Fault-inject or hold a completed result READY long enough to cross the next
capture reset.

Verify:

- the next capture reset still occurs normally;
- capture does not inspect or wait on result publication.

At the next decode trigger, the still-READY single result buffer must produce an
explicit invariant diagnostic/failure rather than a silent normal `skip-busy`
path.

### 4. Decoder-overrun diagnostic

Fault-inject a decoder still RUNNING at the next decode trigger.

Verify:

- prior slot ID and elapsed time are reported;
- this is classified as an invariant fault;
- no benign `skip-busy` policy remains;
- no capture reset was delayed or canceled.

### 5. Live discontinuity during decode

Inject an Audio discontinuity while a decode is active.

Verify:

- no decoder cancel is requested solely for the discontinuity;
- capture does not enter a decoder-dependent wait state;
- the next UTC capture reset occurs;
- later slots remain correctly scheduled.

### 6. Silent slot

Preserve/extend the existing silent-window regression:

- decode completes;
- batch generation advances;
- display generation advances;
- display count becomes zero;
- no previous RX rows are retained as if the slot never occurred.

### 7. Existing regressions

Preserve existing MiniFT8 golden/reference behavior and Linux/ADV builds.

Run at minimum:

```bash
cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"
ctest --test-dir build-linux --output-on-failure

cmake -S tests/unit -B /tmp/T081-unit
cmake --build /tmp/T081-unit -j"$(nproc)"
ctest --test-dir /tmp/T081-unit --output-on-failure

python3 tests/app_dependency_boundary.py . ft8
python3 tests/app_platform_boundary.py . ft8
python3 tests/ft8_platform_boundary.py .
python3 tests/architecture_rules.py .

source ~/projects/esp-idf/export.sh
idf.py -C platform/adv build

git diff --check
```

## Manual / hardware acceptance

Use ADV + QMX on a reasonably active FT8 band.

Enable bounded diagnostics sufficient to identify:

```text
CAPTURE_RESET slot=N
DECODE_START  slot=N
DECODE_DONE   slot=N
RESULT_PUBLISH slot=N
```

Run long enough to cover quiet and busy slots.

Acceptance:

- consecutive receive slot IDs are never missing from capture reset/decode
  start because of decoder/result lifecycle;
- zero-message slots are allowed and still complete;
- RX screen never remains on an older slot merely because a later slot was
  silently dropped;
- observed decode may cross a UTC boundary or the next `-1.60 s` reset without
  disturbing capture;
- no normal `skip-busy` records exist;
- no discontinuity event cancels decode or redefines the FT8 slot schedule;
- QMX RX/TX behavior remains otherwise unchanged.

If an invariant diagnostic occurs, stop acceptance and debug that fault. Do not
add buffering or slot dropping to make the test pass.

## Expected production files

Likely focused changes:

```text
apps/ft8/src/app_controller/app_controller.c
apps/ft8/src/ft8_engine/ft8_engine.[ch]
platform/adv/adv_ft8_decode.c       # only if worker state handoff needs cleanup
tests/ft8_rx_discontinuity_test.c
focused/new RX lifecycle tests
```

Update current MiniFT8 documentation as required by the implementation.

No MiniShell public API change is expected.

## Codex branch / handoff

Work on:

```text
codex/T081-ft8-rx-decouple
```

Start from current `main`.

Read:

```text
AGENTS.md
docs/MiniFT8/README.md
docs/MiniFT8/development.md
docs/MiniFT8/architecture.md
docs/project/codex/I001-continuous-waterfall-rx.md
this task packet
```

Keep T081 to the lifecycle correction described here. Do not implement early
message delivery or deep decoding.

Use one reviewable implementation commit. Do not merge to `main` and do not
open a PR unless asked.

## Codex implementation handoff

### Implementation summary

Live capture reset, UTC anchoring, and decode submission are now independent
UTC actions. The scheduler splits input chunks at -1.60 s, UTC, and +12.64 s;
live framer block-count notifications no longer independently anchor/submit.
This also preserves a damaged slot's decode trigger when missing transport
samples leave its waterfall short. Offline explicit-timing framing is retained.

Producer stream reset no longer cancels/clears an engine decode job. Capture
reset and anchoring never mutate callsign hashes. The first worker step ages
hashes from accepted decode-slot progression (elapsed ages saturate at 255),
with the synchronous tool path following the same rule.

The existing atomic handoff enum is retained: RESULT explicitly means execution
has finished but the single protocol message buffer remains owned until
publication. No extra message buffer, queue, or deep copy was added. The main
RX step consumes completed results before reading more Audio and again after
the drain. A RUNNING job or READY result at a subsequent trigger fails the RX
step with an unconditional `FT8D INVARIANT` diagnostic containing the attempted
slot, prior slot, and decode elapsed time or result age respectively.

Live discontinuities reset frontend conversion only. Startup, offline stream
reset and intentional physical-TX pause/resume retain timing initialization;
intentional TX cancellation remains intact. Normal ADV diagnostic events are
`CAPTURE_RESET`, `DECODE_START`, `DECODE_DONE`, and `RESULT_PUBLISH`.

### Files changed

- `apps/ft8/src/app_controller/app_controller.c`: UTC submission, fault
  diagnostics, publication ordering, live discontinuity policy.
- `apps/ft8/src/ft8_engine/ft8_engine.[ch]`: producer-only reset semantics and
  decoder-owned hash aging.
- `tests/ft8_rx_lifecycle_test.c` and `CMakeLists.txt`: new production-stack
  regression using the ADV 2x1 profile and deterministic worker scheduling.
- `docs/MiniFT8/README.md`, `development.md`, `architecture.md`: current timing
  and ownership contracts, with hardware acceptance explicitly pending.
- This task packet: implementation and validation handoff.

### Behavior/invariants preserved

- 6 kHz, 960-sample blocks, -1.60 s reset, +12.64 s decode, ADV 2x1 geometry,
  50 candidates, FFT/search/LDPC/SNR mathematics unchanged.
- One ADV core-1 worker and the existing single `protocol_messages[50]` storage.
- Empty results advance batch/display generations and clear old RX rows.
- Public MiniShell APIs, AutoSeq, UI layout, CAT/TX policy and intentional
  physical-TX pause/resume are unchanged.
- No platform dependencies or heap allocations introduced into pure modules.
- No architectural deviations. The UTC-trigger split is needed to satisfy the
  packet's damaged-slot contract as well as removal of the busy-skip lifecycle.

### Tests run and results

```text
cmake -S . -B build-linux
cmake --build build-linux -j8
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure
    Initial run PASS: 127/127 (61.00 s).
    Final run: 126/127 (61.11 s); unchanged linux_serial_unit line 67 failed.
    All FT8 golden/reference, lifecycle and physical-TX regressions passed.

ctest --test-dir build-linux -R '^linux_serial_unit$' --output-on-failure
    PASS: 1/1 on the focused check after the final suite failure.

ctest --test-dir build-linux -R 'ft8_rx_(lifecycle|discontinuity|adv_profile)|ft8_engine_rx1g' --output-on-failure
    PASS: 4/4.

cmake -S tests/unit -B /tmp/T081-unit
cmake --build /tmp/T081-unit -j8
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir /tmp/T081-unit --output-on-failure
    PASS: 29/29.

python3 tests/app_dependency_boundary.py . ft8
python3 tests/app_platform_boundary.py . ft8
python3 tests/ft8_platform_boundary.py .
python3 tests/architecture_rules.py .
    PASS: all four commands.

source /home/wei/projects/esp-idf/export.sh
idf.py -C platform/adv build
    PASS: firmware built; 78% of smallest app partition free.

git diff --check
    PASS.
```

The new regression proves five consecutive reset/decode/done/publication slot
sequences, active decode spanning the next reset and UTC anchor, unchanged
worker view/hash state during producer actions, frontend-only discontinuity
handling, UTC submission despite missing samples, next-slot recovery, READY
spanning a reset, distinct late-RUNNING/late-READY failures with exact prior-slot
and 15000-ms diagnostics, producer stream reset retaining a job, empty-result
batch/display clearing, and prompt publication despite an Audio timeout.
Existing explicit-timing discontinuity tests remain unchanged and pass.

### Hardware/manual validation still required

ADV + QMX active-band run covering quiet/busy slots, bounded per-slot diagnostics,
zero-message display clearing, decode across capture reset, and normal physical
RX/TX recovery. No hardware was flashed or RF test performed in this handoff.
Stop acceptance and investigate if an invariant diagnostic occurs.

### Known limitations or risks

The final full-suite run reproduced the previously observed serial PTY timeout
flake at `tests/linux_serial_test.c:67` (`write(...) == MINI_ERR_TIMEOUT &&
n == 0`); both the initial full run and the subsequent focused check passed.
The serial test/backend/service/public API files match base main exactly. No
serial test or implementation was changed to accommodate this failure.

A late decoder can still read waterfall bytes overwritten by the following
producer, as explicitly allowed by this packet; yield may degrade. The tests
hold worker steps deterministically and do not replace multicore/hardware
validation. Device disappearance/re-enumeration remains outside T081. UTC
scheduling is serviced by incoming live Audio chunks, as in the existing design;
this task does not add a separate timer or transport recovery policy.

### Commit reference

One implementation commit titled `T081: decouple FT8 live capture and decode`
on `codex/T081-ft8-rx-decouple`, based on current main
`4238191af6a2553f0feb7385317eb80942eb791d`. The pushed SHA is supplied in the
Codex handoff; main is not merged and no PR is opened.


## Supervisor review

Reviewed `main..c1badcce2ebea226bde1dcfc775db2dfe420c9fe` against T081.

Result: **PASS for software review; advanced to TESTING.**

Key findings:

- live capture reset and decode submission are now UTC-driven recurring actions;
- the old normal `skip-busy` slot-drop path is removed;
- a late RUNNING decoder or unconsumed READY result at the next decode trigger is
  an explicit invariant failure with prior-slot and timing diagnostics;
- live Audio discontinuity resets frontend conversion only and no longer
  requests decoder cancellation or restarts the live slot schedule;
- producer reset no longer clears/cancels decoder-job state;
- callsign-hash aging moved out of producer/UTC anchoring and into decode
  ownership;
- the existing single `protocol_messages[50]` result storage is preserved;
  no extra result buffer, queue, or deep copy was introduced;
- zero-message results still advance batch/display generation and clear stale RX
  rows;
- physical-TX pause/resume retains its intentional cancel/re-anchor behavior;
- DSP/search/LDPC/SNR geometry and policy are unchanged.

The new lifecycle regression directly covers the important T081 failure modes:

```text
consecutive reset/decode/done/publish slots
decode RUNNING across next capture reset
READY result across next capture reset
late RUNNING at next decode trigger -> invariant fault
late READY at next decode trigger -> invariant fault
live discontinuity during decode without cancellation
producer reset retaining active decode
zero-message batch/display completion
prompt result publication before further Audio work
```

Accepted implementation evidence:

```text
focused FT8 lifecycle/regression tests: 4/4 PASS
portable unit tests: 29/29 PASS
ADV ESP-IDF build: PASS
architecture boundary checks: PASS
git diff --check: PASS
Linux full CTest: initial 127/127 PASS;
                   final 126/127 due known linux_serial_unit PTY flake,
                   focused rerun 1/1 PASS
```

The serial failure is outside the T081 diff and does not block hardware
validation.

`main` was fast-forwarded to:

```text
c1badcce2ebea226bde1dcfc775db2dfe420c9fe
```

Remaining gate: ADV + QMX hardware validation on quiet and busy FT8 slots.
Acceptance should confirm consecutive `CAPTURE_RESET` and `DECODE_START`
slot IDs with no normal `INVARIANT` records, correct zero-message behavior,
and normal physical RX/TX recovery.

If any invariant diagnostic appears during hardware testing, stop and debug that
condition. Do not add buffering, backpressure, or slot dropping to hide it.


## Architect hardware acceptance

ADV + QMX live hardware validation passed on 2026-09-25.

The architect reports the decoupled RX behavior worked correctly in live use:
capture continued on its UTC schedule, consecutive receive opportunities were
preserved, and the prior held-RX-slot symptom was no longer observed.

Result: **PASS. T081 COMPLETE.**

The accepted production rule is now:

```text
UTC -1.60 s    reset/fill the slot-local waterfall
UTC +12.64 s   start that slot's decode
repeat every slot

capture never waits for decode/result state
late decoder/result state is an invariant fault, not backpressure
live Audio discontinuity does not cancel decode or redefine slot timing
```

Future early/partial result delivery and deep-decoding work must preserve this
capture/decode decoupling.

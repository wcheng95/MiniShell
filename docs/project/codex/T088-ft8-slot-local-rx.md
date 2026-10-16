# T088 — Slot-local RX/FFT lifecycle with decoupled decode

Status: TESTING

## Intent

Restore MiniFT8-V2's simple slot-local capture window while keeping the useful
V3/T081 lifecycle decoupling:

> Capture/FFT owns the current slot. Decode owns a completed slot. A late decode
> may degrade or become stale, but it must never delay or poison capture of the
> next slot.

The architect promoted T088 directly on 2026-10-15 so the complete active-band
repair can be field-tested as one V3.088 binary after local build/test.

Pinned V2 reference:

```text
wcheng95/Mini-FT8
491e757ae6b1e4cfd2b9a6ba10f48b35643849e0
```

## Target timing model

```text
UTC +0.00 s
    new slot takes capture ownership immediately
    reset slot-local frontend/framer/FFT/waterfall state

0.00 .. 12.64 s
    consume exactly 79 x 960 samples at 6 kHz
    build the slot-local waterfall

UTC +12.64 s
    current waterfall is decode-ready
    submit decode on core 1

12.64 .. 15.00 s
    decoder normally finishes in the 2.36 s remainder
    physical UAC remains alive
    transport is drained/discarded as needed
    no stale post-decode audio accumulates for the next slot

next UTC boundary
    next slot takes capture ownership regardless of prior decode state
    start a fresh slot-local waterfall
```

## Remove the V3 preroll schedule

The production live path should no longer depend on:

```text
UTC -1.60 s producer reset
UTC  0.00 s separate anchor
93-block cross-slot/continuous waterfall lifetime
```

Candidate search may retain the V2-compatible `time_offset=-10..+19` range.
As in V2, negative/pre-slot rows may simply be unavailable and ignored by sync
scoring / likelihood extraction. Do not invent preroll buffering solely to fill
those rows.

## Physical transport rule

QMX UAC remains physically continuous below the MiniShell Audio API.

"Reset at UTC boundary" means reset the MiniFT8 slot-local consumer/DSP state,
not stop/restart or re-enumerate USB/UAC.

Between decode submission and the next UTC boundary, MiniFT8 must not allow old
ring contents to become the next slot's initial samples. Prefer continuous drain
or explicit discard of non-capture-period audio. Do not add a large backlog as a
substitute for correct slot ownership.

## Decode decoupling

The ADV decoder remains a separate core-1 worker.

Normal case:

```text
slot N waterfall complete at +12.64
decode N runs
slot N+1 capture starts at next UTC even if decode N is still finishing
decode N normally completes early in slot N+1
```

Crossing the UTC boundary is therefore allowed and is not itself a fault.

Capture/reset of slot N+1 must not:

- wait for decode N;
- cancel decode N merely because UTC advanced;
- stop physical UAC;
- make the UI/application exit.

The shared old waterfall may be progressively overwritten after the next slot
starts; reduced quality for an abnormally late previous decode is acceptable.
Protecting old decode quality must not add capture backpressure.

## Extreme overrun policy

If a previous decode is still RUNNING, or its result remains unconsumed, at the
following slot's own +12.64 s decode trigger, the previous work is stale.

That condition must become a contained performance-overrun path rather than a
fatal FT8 application invariant.

Desired policy:

```text
current slot wins
stale previous decode/result is canceled/dropped
submit current slot decode as soon as decoder ownership is safely reclaimed
UI and capture continue
```

The exact worker-owned cancellation/pending-submit mechanism is not frozen in
this DRAFT. Preserve ownership: core 0 must not mutate decoder-private state
while core 1 is actively inside candidate/LDPC work.

## Priority rule

During 0..12.64 s:

```text
UAC acquisition
    -> slot sample consumption
    -> frontend/framing
    -> FFT/waterfall
```

must outrank optional:

```text
RxTxLog
UI refresh
diagnostic output
QSO-page work
other noncritical filesystem work
```

T086 and T087 address two known contributors before T088 is implemented.

## Non-goals

Do not include:

- V2 block normalization (postponed separately);
- `freq_osr=2`;
- WebUI;
- speculative/two-pass decoder policy;
- new decoder math;
- extra waterfall banks unless measurement proves necessary;
- physical UAC restart at every slot.

## MiniFT8 version

When promoted and implemented, T088 changes production MiniFT8 code and therefore
must set:

```text
MiniFT8-V3.088
```

with matching UI assertion.

## Acceptance evidence

Before hardware acceptance, run the normal Linux and ADV build/test gates. The
field test should then collect:

```text
candidate count
search time
decode wall time
message count
UAC high-water / overflow
zero-decode occurrences
```

The primary acceptance criterion is elimination of unexplained zero-decode slots
on an active band while normal slot progression, UI responsiveness, and RX/TX
behavior remain intact.

## Likely implementation areas

Expected investigation surface:

```text
apps/ft8/src/app_controller/app_controller.c
apps/ft8/src/rx_slot_framer/
apps/ft8/src/ft8_engine/ft8_engine.c
apps/ft8/src/ft8_engine/ft8_monitor.c
focused RX timing/discontinuity tests
ADV live diagnostics
```

Keep MiniShell Audio as the transport boundary and MiniFT8 as owner of FT8
slot/DSP semantics.


## Implementation handoff

### Implementation summary

Implemented the promoted T088 slot-local live RX lifecycle on
`codex/T088-ft8-slot-local-rx`.

Live scheduling now has only two UTC deadlines per slot:

```text
UTC +0.00 s   reset/begin fresh slot-local capture
UTC +12.64 s  submit decode and stop feeding the slot waterfall
```

Samples between +12.64 s and the next UTC boundary are continuously consumed from
MiniShell Audio/frontend processing but discarded before the framer/FFT path. The
next UTC boundary takes producer ownership immediately even when the previous
core-1 decode is still running.

The old live `-1.60 s` preroll/reset plus separate UTC anchor sequence is no
longer used by `app_controller`.

Late-decode policy is now contained:

- crossing the next UTC boundary is normal;
- if an old RUNNING decode survives until the following slot's +12.64 s trigger,
  core 0 requests cancellation and records the current slot as pending;
- core 1 performs its own engine cancellation;
- the pending current-slot decode starts when ownership returns;
- an unconsumed stale READY result is dropped rather than terminating RX.

V2-compatible negative time-offset search is retained without preroll storage:
unavailable early rows are skipped and later Costas rows still contribute.

MiniFT8 About is `MiniFT8-V3.088`.

### Files changed

```text
apps/ft8/src/app_controller/app_controller.c
apps/ft8/src/ft8_engine/ft8_decoder.c
apps/ft8/src/rx_slot_framer/rx_slot_framer.h
apps/ft8/src/ui_shell/ui_shell.c
tests/ft8_rx_lifecycle_test.c
tests/ft8_decoder_rx1d_test.c
tests/ft8_ui_smoke.c
docs/MiniFT8/README.md
docs/MiniFT8/architecture.md
docs/MiniFT8/development.md
docs/project/codex/T088-ft8-slot-local-rx.md
```

### Behavior/invariants preserved

- physical QMX UAC remains continuously running below MiniShell Audio;
- MiniShell remains transport owner; MiniFT8 owns FT8 slot/DSP timing;
- ADV decoder remains core 1 / priority 4 / static 8192-byte stack from T087;
- T086 RxTxLog no-sync policy remains unchanged;
- candidate capacity 50, minimum score 5, strongest-first ordering, BP-LDPC and
  25-iteration limit remain unchanged;
- no block normalization, freq_osr=2, WebUI, speculative decode, extra waterfall
  bank, or physical UAC slot restart was added;
- explicit-timing/offline RX path remains separate from the live scheduler.

### Test changes

`ft8_rx_lifecycle_test` now covers:

- consecutive slot-local 79-block capture;
- decode crossing a UTC boundary while the next slot captures;
- a transport gap not moving UTC deadlines;
- stale RUNNING decode cancellation/pending handoff;
- stale READY result drop;
- startup mid-slot waiting for the next complete slot;
- prompt result publication.

`ft8_decoder_rx1d_test` adds a no-preroll negative-time candidate case.

The normal About assertion is updated to V3.088.

### Validation status

Source-level review completed:

- no remaining live `RX_SLOT_FRAMER_PREROLL_SAMPLES` dependency in
  `app_controller`;
- no old fatal `INVARIANT` trigger path remains there;
- no stale `live_begin_emitted` state remains;
- modified files have no trailing whitespace.

The supervisor environment cannot execute the repository's Linux/ESP-IDF build.
Run the normal pc-1 gates before merge/flash:

```text
cmake -S . -B build-linux
cmake --build build-linux -j8
ctest --test-dir build-linux --output-on-failure
git diff --check

source /home/wei/projects/esp-idf/export.sh
idf.py -C platform/adv build
```

### Hardware/manual validation still required

Field-test V3.088 on an active band. The blocker criterion is zero-decode:
strong/active FT8 slots must not intermittently collapse to zero decoded messages
because of prior-slot load.

Also verify normal:

```text
CAPTURE_RESET -> DECODE_START -> DECODE_DONE -> RESULT_PUBLISH
```

slot progression, UI responsiveness, UAC high-water/overflow, and RX/TX behavior.

### Known limitations or risks

A previous decode that crosses the next UTC boundary still reads the same
waterfall allocation while the new producer gradually overwrites it. This is
intentional and may reduce only the stale previous slot's late decode yield;
current-slot capture always wins.

The implementation has not yet been compiled in this supervisor environment, so
pc-1 Linux/ADV build evidence is required before fast-forwarding main.

### Commit reference

The branch is delivered as one squashed T088 implementation commit; the full SHA
is supplied in the chat handoff.


## Bench acceptance

Architect bench test on ADV/QMX after flashing V3.088:

- no functional breakage observed;
- decoding is noticeably faster;
- V3.088 is promoted as the new MiniFT8 baseline;
- active-band field validation of the zero-decode blocker remains pending.

T088 therefore remains in TESTING until the field test establishes whether
unexplained zero-decode slots are eliminated under real crowded-band load.

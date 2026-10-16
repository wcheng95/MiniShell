# T088 — Slot-local RX/FFT lifecycle with decoupled decode

Status: DRAFT

## Intent

Restore MiniFT8-V2's simple slot-local capture window while keeping the useful
V3/T081 lifecycle decoupling:

> Capture/FFT owns the current slot. Decode owns a completed slot. A late decode
> may degrade or become stale, but it must never delay or poison capture of the
> next slot.

This task is intentionally DRAFT until T086/T087 field evidence is available.
Do not implement it yet unless the architect promotes it to READY.

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

## Evidence required before READY

Review T086/T087 active-band results:

```text
candidate count
search time
decode wall time
message count
UAC high-water / overflow
zero-decode occurrences
```

If zero-decode disappears completely and repeated active-band operation is
stable after T086/T087, the architect may decide whether T088 is still worth
implementing as simplification. If zero-decode remains, T088 becomes the next
correctness repair.

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

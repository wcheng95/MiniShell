# MiniFT8-V3 RX-4 — Streaming slot framer

Status: **COMPLETE**

RX-4 implements the MiniFT8-owned boundary between a continuous 6 kHz engine-native sample stream and the explicit `Ft8Engine` window/block lifecycle.

RX-4 remains MiniShell-independent and platform-independent. Development and validation are Linux-only; the ADV backend is not involved.

## Goal

The RX-1B timing rule is now executable:

> UTC/sample timing establishes the initial FT8 slot identity and phase. After that, sample count owns progress. A partial first slot is discarded rather than presented as a complete decode window.

The resulting path is:

```text
continuous 6 kHz mono float
        |
        v
RxSlotFramer
    slot identity
    sample-count progression
    partial-first-slot suppression
    one 960-sample accumulator
        |
        +--> BEGIN_WINDOW(slot_id)
        +--> ENGINE_BLOCK(slot_id, 960 samples)
        +--> FINALIZE_WINDOW(slot_id)
        `--> STREAM_RESET(new reference)
        |
        v
Ft8Engine
```

## Ownership

`RxSlotFramer` owns:

```text
current slot identity
sample offset inside the current slot
first-partial-slot suppression state
one bounded 960-float block accumulator
the ordering of begin/block/finalize/reset events
faulted state after downstream event rejection
```

It does not own:

```text
MiniShell Time
MiniShell Audio
UTC clock access
12 kHz frontend conversion
Ft8Engine state/workspace
message/result storage
UI / AutoSeq / TX
```

The caller remains responsible for deriving the initial `(slot_id, sample_offset)` timing reference. In later application integration, `app_controller` will derive that reference from MiniShell Time and then let the framer advance purely by sample count.

## Exact FT8 framing

Current engine-native rate:

```text
6000 samples/s
```

One FT8 slot:

```text
15 s * 6000 = 90000 samples
```

One `Ft8Engine` monitor block:

```text
960 samples = 160 ms
```

Therefore a complete slot contains:

```text
93 complete blocks = 89280 samples
slot-end remainder =   720 samples
                       -----
                       90000 samples
```

The 720-sample remainder is discarded at the slot boundary. It is never carried into the next FT8 window because doing so would mix samples from different UTC slots.

The monitor's normal new-window transition preserves its analysis history; the framer itself does not manufacture a cross-slot partial PCM block.

## First partial slot

Initialization takes:

```text
slot_id
sample_offset = 0..89999
```

If `sample_offset == 0`, the first incoming sample belongs to a complete window and `BEGIN_WINDOW(slot_id)` is emitted before consuming it.

If `sample_offset != 0`, RX-4 discards samples until the next 90000-sample boundary. No begin/block/finalize events are generated for that partial first slot. The next sample then starts `slot_id + 1` as the first complete decode window.

This implements the RX-1B contract rather than attempting to decode a window known to be incomplete.

## Stream discontinuity

A stream discontinuity is explicit:

```text
rx_slot_framer_reset_stream(new_slot_id, new_sample_offset)
    -> discard partial 960-sample accumulator
    -> discard active-window framing state
    -> emit STREAM_RESET
    -> establish new timing reference
    -> suppress first partial slot when offset != 0
```

The downstream assembly maps `STREAM_RESET` to `ft8_engine_reset_stream()`, which clears DSP continuity while preserving persistent protocol/hash knowledge.

A normal slot transition does not emit `STREAM_RESET`.

## Chunk invariance

Transport/frontend chunk sizes have no framing meaning. RX-4 tests feed the same two-slot sample sequence using very different chunk sizes and require identical:

```text
BEGIN/FIT8 block/FINALIZE counts
slot identities
emitted sample count
emitted sample fingerprint
```

This proves that caller chunking is not observable at the engine boundary.

## Failure containment

The event callback returns success/failure.

If a downstream sink rejects an event, the framer becomes faulted and stops accepting normal stream data. It does not continue consuming samples and hope that downstream state can catch up.

Recovery requires an explicit `reset_stream()` with a fresh timing reference.

This preserves MiniShell/MiniFT8's fault-containment principle: one failed boundary is reported upward rather than repaired by reaching into another owner's private state.

## Tests

`tests/rx_slot_framer_rx4_test.c` covers:

```text
invalid initialization
zero-length input
exact 90000-sample slot
93 complete blocks + discarded 720-sample remainder
partial-first-slot suppression
arbitrary chunk-size invariance
two consecutive slots
explicit stream reset
partial-block discard on reset
sink failure -> faulted state -> reset recovery
```

Dedicated RX-4 CI compiles the unit test with strict warnings:

```text
-Wall -Wextra -Werror -Wpedantic
```

and reports:

```text
rx_slot_framer_rx4_test: PASS
```

## Engine golden

`tests/rx_slot_framer_rx4_reference.c` uses the pinned MiniFT8-V2 reference WAV:

```text
ft8_cq_w1xyz_fn42.wav
```

The WAV begins at an artificial slot boundary. RX-4 streams it in 257-sample chunks and pads the remaining portion of the 15-second slot with silence. The framer—not the test harness—decides all 960-sample engine blocks and the final slot boundary.

Frozen result:

```text
samples = 90000
engine blocks = 93
slot_id = 12345
message = CQ W1XYZ FN42
```

CI output:

```text
RX4 samples=90000 blocks=93 slot=12345 text="CQ W1XYZ FN42"
rx_slot_framer_rx4_reference: PASS
```

This proves the cleaned framing boundary can drive the complete RX-1 `Ft8Engine` without changing the frozen decode result.

## Explicitly deferred

RX-4 does not add:

```text
MiniShell Audio lifecycle
MiniShell Time reads inside the framer
12 kHz conversion (RX-3 already owns that)
station-aware RxBatch construction
UI integration
ADV backend audio
AutoSeq / TX / logging
DSP/filter/search changes
```

## Next

RX-5 assembles the pure MiniFT8 RX pieces above MiniShell:

```text
12 kHz S16 stereo
    -> RxFrontend
    -> 6 kHz mono float
    -> RxSlotFramer
    -> Ft8Engine
    -> Ft8ProtocolSlot
    -> rx_result_builder
    -> RxBatch
```

RX-5 remains on Linux and uses no real ADV backend dependency.

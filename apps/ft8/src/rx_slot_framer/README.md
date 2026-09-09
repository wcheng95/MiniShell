# RX slot framer

`rx_slot_framer` is the MiniFT8-owned streaming boundary between continuous 6 kHz engine-native samples and `Ft8Engine` window/block lifecycle.

It owns:

```text
initial slot/sample timing reference
sample-count progress after that reference
first-partial-slot suppression
one bounded 960-sample accumulator
15-second slot transitions
BEGIN_WINDOW / ENGINE_BLOCK / FINALIZE_WINDOW / STREAM_RESET events
```

It does **not** own:

```text
MiniShell Time
MiniShell Audio
UTC clock reads after initialization
frontend conversion/decimation
Ft8Engine DSP state
protocol decoding
UI / AutoSeq / TX
```

## Timing model

A caller establishes the first sample with:

```text
slot_id
sample_offset = 0..89999 at 6 kHz
```

For live RX, `app_controller` will later derive this reference from MiniShell Time. From that point forward, sample count is authoritative.

If the stream begins inside a slot (`sample_offset != 0`), that first partial slot is discarded. The first decode window begins only at the next complete FT8 boundary.

One FT8 slot is:

```text
15 s * 6000 samples/s = 90000 samples
```

The engine accepts 960-sample monitor blocks. Therefore one full slot produces:

```text
93 complete blocks = 89280 samples
slot-end remainder =   720 samples
```

The 720-sample remainder is discarded at the slot boundary. It is never carried across the boundary into the next engine window.

## Event contract

```text
BEGIN_WINDOW(slot_id)
ENGINE_BLOCK(slot_id, 960 samples)
...
FINALIZE_WINDOW(slot_id)
```

On a discontinuity:

```text
STREAM_RESET(new_reference_slot_id)
```

and a new `(slot_id, sample_offset)` reference is established.

The block pointer in an `ENGINE_BLOCK` event is borrowed storage owned by the framer and is valid only during the callback.

If the event sink fails, the framer enters a faulted state and stops accepting ordinary samples. Recovery requires an explicit stream reset with a fresh timing reference; the framer does not attempt to repair downstream state itself.

## RX-4 invariants

- no whole-slot PCM buffer;
- arbitrary input chunk sizes are invisible to framing;
- no sample is duplicated into two slots;
- no partial 960-sample block crosses an FT8 slot boundary;
- a partial first slot is never presented as a complete decode window;
- normal slot transitions and stream discontinuities remain distinct operations;
- the module is MiniShell-independent and platform-independent.

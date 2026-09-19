# RX slot framer

`rx_slot_framer` is the MiniFT8-owned timing boundary between the continuous
6 kHz engine-native sample stream and UTC FT8 slot anchors.

I001 changed this module from a per-slot block window into a continuous
blockizer. **A normal 15-second UTC boundary never discards audio and never
resets the 960-sample accumulator.**

It owns:

```text
initial slot/sample timing reference
continuous 960-sample blockization
15-second UTC slot transitions
logical slot-anchor state
+79 candidate-search trigger
BEGIN_WINDOW / ENGINE_BLOCK / FINALIZE_WINDOW / STREAM_RESET
```

It does not own MiniShell Time/Audio, DSP/waterfall storage, protocol decoding,
UI, AutoSeq, or TX.

## Timing model

A caller establishes the first sample with:

```text
slot_id
sample_offset = 0..89999 at 6 kHz
```

If the stream begins inside a slot, samples are still blockized immediately so
history is preserved, but that partial slot does not receive a decode anchor.
The first usable anchor is latched at the next UTC boundary.

One FT8 slot is:

```text
15 s * 6000 samples/s = 90000 samples
```

The engine block is 960 samples / 160 ms, therefore:

```text
90000 = 93 * 960 + 720
```

The old RX-4 design discarded those 720 samples. I001 deliberately carries
them across the UTC boundary. A 960-sample block may therefore straddle two FT8
slots.

At a UTC boundary, the framer emits `BEGIN_WINDOW` before consuming the first
post-boundary samples. The downstream engine latches the beginning of the
currently filling waterfall block as the logical slot origin. The resulting
<160-ms quantization is intentional and is absorbed by candidate timing search.

## Event contract

```text
BEGIN_WINDOW(slot_id)        latch logical slot origin
ENGINE_BLOCK(...960...)      continuous monitor block
...
FINALIZE_WINDOW(slot_id)     candidate search after 79 anchored blocks
```

The historical `FINALIZE_WINDOW` name is retained for source compatibility;
it no longer means "consume/reset the waterfall."

On a real stream discontinuity:

```text
STREAM_RESET(new_reference_slot_id)
```

The partial 960-sample accumulator is then discarded and downstream DSP
continuity is reset. Ordinary UTC slot transitions never use this path.

The block pointer in `ENGINE_BLOCK` is borrowed storage owned by the framer and
is valid only during the callback. If the event sink fails, the framer faults
until an explicit stream reset.

## I001 invariants

- no whole-slot PCM buffer;
- arbitrary input chunk boundaries have no framing meaning;
- the 960-sample blockizer is continuous across normal UTC boundaries;
- no 720-sample slot remainder is discarded;
- a partial startup slot fills history but is never decoded;
- candidate search is triggered after 79 completed blocks relative to the anchor;
- normal slot transitions and real stream discontinuities remain distinct;
- the module remains MiniShell-independent and platform-independent.

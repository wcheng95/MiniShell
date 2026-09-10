# MiniFT8-V3 RX Tuning

This record tracks intentional receive-performance changes made after the RX-1A through RX-7 structural/golden cleanup.

## Production default

MiniFT8-V3 now uses:

```text
time_osr = 2
freq_osr = 2
```

The earlier RX-1C/V2-compatible configuration remains the low-memory reference/fallback:

```text
time_osr = 2
freq_osr = 1
```

The historical RX-1C document remains unchanged because RX-1C correctly describes the configuration that was frozen during that cleanup stage.

## ADV measurement

Fixture:

```text
Linux source: tests/kfs16b12k.wav
ADV copy:     /sd/kfs.wav
```

Measured on Cardputer ADV after the MiniShell/ESP-IDF RAM Phase A+B tuning:

```text
                       2x1          2x2
messages decoded        13           16
app allocation       125.5 KiB    228.5 KiB
heap free            215.3 KiB    111.3 KiB
largest free block   156.0 KiB     53.0 KiB
```

The 2x2 configuration decoded 3 additional messages on this fixture, a 23% increase, at a cost of about 103 KiB additional application memory.

The 2x2 monitor uses a 1920-point FFT. On the Linux reference build its monitor workspace is about 211 KiB; exact embedded workspace size is queried at runtime rather than hard-coded.

## Test policy

Production tests exercise the 2x2 default.

The monitor golden test also retains the old 2x1 configuration and byte fingerprint so the V2-compatible low-memory boundary is not lost while the production default advances.

## RAM policy for later MiniFT8 work

The 2x2 receive path leaves useful but finite headroom on ADV, so future features must remain RAM-conscious, especially repeated per-QSO/per-entry structures.

For AutoSeq, active-QSO state, hash tables, candidate metadata, and similar bounded state:

- prefer narrow integer types when the value range is known;
- prefer bit masks/bitsets for repeated boolean state;
- combine naturally related boolean flags into compact flag words rather than storing many `int`/`bool` fields;
- avoid padding-heavy structs in arrays and tables; order fields deliberately and measure `sizeof`;
- allocate large temporary working storage only for the phase that needs it and release it when possible;
- do not bit-pack merely to save a few bytes when it makes correctness, atomic access, or timing-critical code harder to reason about.

The goal is not minimum RAM at all costs. The goal is to preserve enough headroom for stronger decoding and optional services such as temporary Wi-Fi/PSKReporter upload.

## Fallback rule

If later features cannot coexist safely with 2x2 on ADV, the preferred order is:

1. reduce/compact the new feature's resident state;
2. release phase-specific allocations before starting another high-memory service;
3. use the deferred ADV RAM Phase C only if justified by measurement;
4. fall back to `time_osr=2, freq_osr=1` only when the operational benefit outweighs the measured decode loss.

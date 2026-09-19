# I001 — Continuous FT8 waterfall + two-stage candidate search

Status: IMPLEMENTED, SOFTWARE/HARDWARE VALIDATION PENDING

## Why this is an improvement

ADV/QMX testing after T030 completed the first real MiniFT8-V3 QSO but exposed
an RX architecture limitation: synchronous FT8 decoding could occupy slightly
more than the nominal 2.36-second gap between the 12.64-second decode trigger
and the next 15-second UTC slot. The visible symptom was a >2-second UI clock
stall and loss of every other receive opportunity. The skipped slot was not
tied to odd/even parity; either parity could be lost.

I001 is the first architect-originated MiniFT8-V3 improvement. It is not a CAT
or QMX transport fix.

## Architect decisions

1. The FT8 waterfall is a continuous circular timeline. Normal UTC slot
   boundaries and decode events never reset it.
2. At each valid UTC 15-second boundary, latch the beginning of the currently
   filling 160-ms waterfall block as that slot's logical origin.
3. Exact sample-phase alignment is intentionally unnecessary. The resulting
   <160-ms origin error is handled by the existing candidate timing search.
4. Use a 96-KiB minimum waterfall allocation for the ADV low-memory profile.
   The portable 2x2 profile must still retain at least 94 complete blocks.
5. Keep the existing candidate timing search:
   `time_offset=-10..19`, `time_sub=0..time_osr-1`.
6. Search #1 always runs after 79 completed logical blocks and produces the
   original top 50 candidates.
7. LDPC attempts remain highest-score first.
8. Search #2 always runs after 86 completed logical blocks.
9. Search #2 contributes only the five highest-score candidate identities that
   were not in the original Search #1 top 50. Candidate identity is exactly
   `(time_offset, time_sub, freq_offset, freq_sub)`.
10. The five refinement candidates are appended; original candidates are never
    re-decoded merely because their later score improved.
11. No likelihood buffer is added. Candidate likelihood extraction and LDPC
    continue to read the retained live waterfall directly.
12. Only one slot decode job is active at a time. If a previous job is still
    active when a new slot reaches Search #1, that new slot's decode may be
    skipped. Capture continuity must never wait for LDPC.
13. Startup has no special partial-slot decode. Audio/waterfall history may fill
    immediately, but decoding starts only after the first valid UTC boundary.
14. Do not change ADV task core/priority as part of I001.

## Timing/search basis

FT8 monitor parameters:

```text
sample rate       6000 Hz
block             960 samples = 160 ms
ADV time_osr      2
ADV freq_osr      1
bins              433
ADV block stride  866 bytes
```

The existing candidate time grid means:

```text
time = (time_offset + time_sub / 2) * 160 ms
time_offset=-10..19
time_sub=0..1
search grid = -1.60 s .. +3.12 s in 80-ms steps
```

At Search #1, continuous pre-slot history gives negative timing hypotheses real
data instead of an artificial boundary. Late hypotheses may still have
incomplete final Costas sync, which is why Search #2 is delayed to 86 blocks.

## Continuous framing rule

The old RX-4 framer discarded the 720-sample remainder:

```text
15 s * 6000 = 90000 samples
90000 = 93 * 960 + 720
```

I001 removes that discard. The 960-sample blockizer is independent of UTC slot
boundaries, so a block may straddle adjacent slots. A UTC boundary latches the
beginning of that in-progress block.

## Waterfall capacity

The ring allocation is:

```text
max(96 KiB, 94 * block_stride)
```

Therefore:

```text
ADV 2x1:  98304 / 866 = 113 complete blocks ~= 18.08 s
base 2x2: 94 * 1732   = 162808 bytes, 94 complete blocks
```

The larger portable allocation is required because 96 KiB alone cannot retain
a full FT8 slot at freq_osr=2.

## Decode scheduling

```text
UTC boundary
    -> latch slot anchor

anchor + 79 blocks
    -> Search #1
    -> top 50
    -> begin incremental LDPC

anchor + 86 blocks
    -> Search #2
    -> append top 5 identities absent from original 50

audio capture / waterfall fill
    -> never waits for the whole LDPC job
```

The application services at most one LDPC candidate per RX step instead of
running all candidates in one synchronous loop.

Before each incremental LDPC attempt, live RX drains up to eight immediately
available transport chunks with zero wait. This keeps capture ahead of decode
without adding a platform-specific Audio API or making LDPC wait for the source
to become completely empty.

## Files changed

Production:

- `apps/ft8/src/rx_slot_framer/rx_slot_framer.[ch]`
- `apps/ft8/src/ft8_engine/ft8_monitor.[ch]`
- `apps/ft8/src/ft8_engine/ft8_decoder.c`
- `apps/ft8/src/ft8_engine/ft8_engine.[ch]`
- `apps/ft8/src/app_controller/app_controller.c`

Tests/golden compatibility:

- `tests/ft8_monitor_rx1c_test.c`
- `tests/ft8_engine_rx1g_test.c`
- `tests/rx_slot_framer_rx4_test.c`
- `tests/ft8_rx_discontinuity_test.c`
- `tests/rx_slot_framer_rx4_reference.c`
- `tests/rx5_pure_assembly_reference.c`

No MiniShell public API changes are part of I001.

## Validation required

Software gates:

```bash
cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"
ctest --test-dir build-linux --output-on-failure

cmake -S tests/unit -B /tmp/I001-build-unit
cmake --build /tmp/I001-build-unit -j"$(nproc)"
ctest --test-dir /tmp/I001-build-unit --output-on-failure

python3 tests/app_dependency_boundary.py . ft8
python3 tests/app_platform_boundary.py . ft8
python3 tests/ft8_platform_boundary.py .
python3 tests/architecture_rules.py .

source ~/projects/esp-idf/export.sh
idf.py -C platform/adv build
git diff --check
```

ADV/QMX hardware acceptance:

- consecutive receive opportunities are no longer systematically lost every
  other slot; this is not an odd/even-specific test;
- Search #1 occurs after 79 anchored blocks;
- Search #2 occurs after 86 anchored blocks;
- the UI clock no longer freezes for multiple seconds during decode;
- record candidate-search and per-candidate/total LDPC timing;
- record UAC high-water/overflow/discontinuity counters;
- normal QSO RX/TX behavior remains intact.

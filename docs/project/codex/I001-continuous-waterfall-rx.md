# I001 — Continuous FT8 waterfall + incremental decode

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
6. Candidate search starts after 79 completed logical blocks and keeps the top
   50 candidates, highest score first. The original search order and scoring
   are preserved, but the 25,560-position ADV grid is serviced incrementally at
   at most 1,024 positions per RX service step.
7. LDPC is incremental: at most one candidate is attempted per RX service step.
8. No likelihood buffer is added. Candidate likelihood extraction and LDPC
   continue to read the retained live waterfall directly.
9. Only one slot decode job is active at a time. If a previous job is still
   active when a new slot reaches the 79-block search point, that new slot's
   decode may be skipped. Capture continuity must never wait for LDPC.
10. Startup has no special partial-slot decode. Audio/waterfall history may fill
    immediately, but decoding starts only after the first valid UTC boundary.
11. Initial single-core scheduling was evaluated first. ADV hardware decoded,
    but decode work still disturbed the adjacent slot. The follow-up experiment
    therefore keeps the FT8 app/RX path on core 0 and moves only candidate
    search + LDPC to a priority-1 worker pinned to core 1.
12. The UAC capture worker remains priority 4 and unpinned, so it can preempt
    the core-1 decode worker. No UAC priority, FIFO, ring size, or driver-core
    policy changes are part of this experiment.

### Removed refinement experiment

An experimental second candidate search at +86 blocks appended the five
highest-score identities not present in the original top 50. Runtime evaluation
showed no additional decoded messages from those five candidates. Because the
first search already benefits from continuous pre-slot/post-frame waterfall
history, the +86 search, its five extra candidates, and its temporary debug
display were removed.

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

At the 79-block search point, continuous pre-slot history gives negative timing
hypotheses real data instead of an artificial boundary. Candidate scoring also
sees the continuously retained post-frame data already available at that point.

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

ADV experimental split:

```text
core 0                                  core 1
-----------------------------------     ------------------------------
UAC consumer / frontend                 FT8 decode worker, priority 1
960-sample framing                      candidate search
FFT / continuous waterfall              LDPC / CRC / message decode
UTC slot anchoring
UI/application loop

UAC capture worker: priority 4, unpinned; may preempt decode on core 1.
```

At anchor +79 blocks, core 0 snapshots the slot-relative waterfall metadata and
starts one decode job. Core 1 services the existing incremental candidate search
and one-candidate LDPC steps. Core 0 remains the only writer of monitor/FFT state.

The worker reads only the fixed +79 waterfall view. It no longer calls
`ft8_monitor_get_waterfall_at()` while decoding. Callsign-hash aging is deferred
until the next accepted decode job so a UTC boundary on core 0 does not mutate
the hash store while core 1 is decoding.

The application services only one bounded decode unit per RX step instead of
running either candidate search or all LDPC candidates synchronously. During
search that unit is at most 1,024 score positions; after search it is at most
one LDPC candidate.

The existing live-RX drain remains on core 0 and can empty up to eight
immediately available transport chunks per application step. Decode no longer
depends on those drain opportunities for scheduling because it runs
independently on core 1.

### Core-1 worker stack correction

The first ADV core-split build allocated an 8 KiB static stack buffer but passed
`sizeof(stack) / sizeof(StackType_t)` to `xTaskCreateStaticPinnedToCore()`.
ESP-IDF defines that argument in bytes, so the task was configured for only
2048 bytes. That can cause stack overflow or memory corruption despite the
larger backing array. The worker now passes `sizeof(stack)` (8192 bytes) and
reports `uxTaskGetStackHighWaterMark(NULL)` minimum-free bytes on UART.

## ADV decode timing diagnostics

The packaged ADV build enables decode diagnostics through MiniShell
`System.write`. During the QMX USB-host session this is the dedicated UART0
diagnostic path at 115200 8N1 on TX=GPIO3 / RX=GPIO6. The UI is unchanged.

Representative records:

```text
FT8D start       slot=123 ms=0    cand=0  msg=0 state=1
FT8D search-done slot=123 ms=420  cand=50 msg=0 state=1
FT8D done        slot=123 ms=3270 cand=50 msg=4 state=2
FT8D publish     slot=123 ms=3280 cand=50 msg=4 state=0
FT8D skip-busy   slot=124 ms=15000 cand=0 msg=0 state=1
```

All `ms=` values are elapsed from the slot's +79-block decode trigger. Thus
`search-done` measures candidate-search latency, while
`done - search-done` approximates total likelihood/LDPC/message-decode time.
A `skip-busy` line is direct evidence that the previous slot was still
occupying the core-1 decode worker when the next slot reached its own +79
trigger.

The ADV worker also logs its actual core, priority, configured stack bytes, and
new minimum-free stack watermarks. At publication, `FT8D retention` reports
how many 160-ms waterfall blocks remain before logical block -10 would be
overwritten by the circular ring. A zero or negative margin means the decode
job outlived the full timing-search retention guarantee.

Every live UAC discontinuity now also reports the ring high-water mark,
overflow/loss counters, and USB read/transfer-error counters before the stream
restart. This distinguishes consumer starvation from a transport failure at the
point where a receive slot can be lost.

The T017 build-local UAC patch distinguishes recoverable skipped isochronous
packets from true transport loss. A `USB_TRANSFER_STATUS_SKIPPED` packet is
replaced in the native UAC ring by exactly its requested byte count of zeros,
preserving the sample timeline without restarting UAC. For QMX 48 kHz,
24-bit stereo this is normally 288 bytes = 48 frames = 1 ms. Other bad-isoc
statuses, native-ring overflow/push failure, resubmit failure, and general
transfer errors still report a discontinuity.

The 600-byte native-ring threshold experiment did not change the skipped-isoc
behavior; V2 was subsequently observed to receive the same status-6 skipped
packets while continuing normally. The fault therefore predates V3's restart
policy. V3 now preserves timing by padding skipped packets rather than promoting
them into a whole-stream discontinuity.

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
- candidate search occurs after 79 anchored blocks;
- the UI clock no longer freezes for multiple seconds during decode;
- decoded messages may appear after the following UTC boundary, but the
  following slot's RX/FFT/clock must remain smooth;
- record candidate-search and per-candidate/total LDPC timing;
- record UAC high-water/overflow/discontinuity counters;
- normal QSO RX/TX behavior remains intact.


Candidate-search performance note: the continuous circular waterfall originally
resolved logical-to-physical ring addressing for every Costas-symbol access,
including 64-bit modulo in the scoring hot path. V2 uses direct row pointer
arithmetic and measured about 138 ms for the same 25,560-position search,
versus about 1.51 s in V3. The V3 search now precomputes the required logical
waterfall row pointers once per search step, leaving circular modulo outside the
per-candidate scoring loop while preserving the same search order and heap
semantics.


Candidate-search follow-up: the first circular-row optimization reduced ADV
`search-done` from about 1.51 s to about 235 ms. That timestamp still included
the V3 noise-floor histogram pass, unlike V2's `Candidates found` log. The
search now caches the at-most 75 Costas comparison pointer pairs once per
time/sub-lane and reuses them across the innermost frequency sweep. The
noise-floor pass is serviced in the following decode step so `FT8D search-done`
now measures candidate-grid work only. The 1024-position slice size and worker
yield policy remain unchanged for an isolated hardware comparison.

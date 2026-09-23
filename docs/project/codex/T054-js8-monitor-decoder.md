# T054 — JS8 Normal monitor and payload decoder

Status: COMPLETE

## Architect intent

Continue JS8Chat using the proven MiniFT8 receive architecture, changing only what the JS8 Normal PHY requires.

T052 established CRC-12 + LDPC(174,87). T053 established the exact JS8 Normal 79-tone channel mapping.

T054 is the first receive-DSP stage. It must remain KISS and **Normal/Mode A only**.

The architect's real upstream fixture:

```text
~/projects/js8chat/A_2_1.wav
```

is deliberately reserved for T055. T054 proves the receive architecture with deterministic synthetic Normal-mode audio first.

## Objective

Implement the pure JS8 Normal receive path through the validated 75-bit payload boundary:

```text
6 kHz mono float PCM
    -> compact MiniFT8-style waterfall
    -> Normal Costas candidate search
    -> direct-binary 8-FSK likelihood extraction
    -> T052 BP LDPC(174,87)
    -> T052 CRC-12
    -> exact 75-bit payload
```

The implementation architecture is MiniFT8. Do **not** port JS8Call-improved's desktop `DecodeMode<ModeA>`, 60-second PCM buffer, FFTW workspace, Qt threading, whitening/soft-combiner framework, or multi-submode abstractions.

## Accepted baseline

```text
T052  CRC-12 + LDPC(174,87)       COMPLETE
      39023694a59ad06ef86051d98931d79703e67e3c

T053  Normal 79-tone encoder      COMPLETE
      28cf927706e7a1db88ef16a4b7007126634b65f5
```

## Source of truth

Read before editing:

```text
AGENTS.md
docs/js8/README.md
docs/js8/architecture.md
docs/js8/implementation-plan.md
docs/js8/js8-phy.md
docs/project/codex/T052-js8-crc-ldpc.md
docs/project/codex/T053-js8-channel-encoder.md

apps/js8chat/src/js8_engine/js8_crc.[ch]
apps/js8chat/src/js8_engine/js8_ldpc.[ch]
apps/js8chat/src/js8_engine/js8_channel.[ch]

apps/ft8/src/ft8_engine/README.md
apps/ft8/src/ft8_engine/ft8_monitor.[ch]
apps/ft8/src/ft8_engine/ft8_decoder.[ch]
docs/MiniFT8/rx-1c-monitor.md
docs/MiniFT8/rx-1d-decoder.md
```

Frozen wire reference remains:

```text
JS8Call-improved/JS8Call-improved
tag v3.0.3
```

Use upstream for PHY constants/semantics, but use MiniFT8 for receive implementation architecture.

## Key Normal-mode equivalence

At 12 kHz, JS8 Normal uses:

```text
1920 samples/symbol
79 symbols
6.25 Hz tone spacing
```

At the MiniFT8 engine rate of 6 kHz this becomes:

```text
960 samples/symbol
160 ms/symbol
79 symbols
6.25 Hz tone spacing
```

Therefore the current MiniFT8 monitor geometry is directly applicable.

## Architectural constraints

1. **JS8 Normal only.**
   - No B/C/E/I modes.
   - No generic submode abstraction.

2. Pure host-testable C.

3. No heap inside JS8 engine modules.

4. No mutable DSP singleton.

5. Caller-owned/queryable monitor workspace.

6. No MiniShell API, Linux/POSIX audio, ESP-IDF, NuttX, UI, Time, Radio, filesystem, or application protocol dependency.

7. Do not modify FT8 production code.

8. Do not refactor FT8 into a shared generic engine yet.

9. Preserve the MiniFT8 receive model:
   - 6 kHz mono float engine edge;
   - exact 960-sample blocks;
   - compact uint8 waterfall;
   - time/frequency oversampling;
   - bounded candidate list;
   - safe logical waterfall indexing;
   - exact payload is the RX identity at this boundary.

10. No JS8Call desktop raw-audio/FFTW architecture.

11. Candidate score/order are diagnostics, not interoperability identity. Exact decoded 75-bit payload is authoritative.

## Monitor scope

Add JS8-owned monitor files, for example:

```text
apps/js8chat/src/js8_engine/
    js8_monitor.c
    js8_monitor.h
```

Follow the current MiniFT8 monitor ownership/lifecycle design.

Baseline host configuration:

```text
sample rate       6000 Hz
block size         960 samples
symbol duration    160 ms
f_min              200 Hz
f_max             2900 Hz
time_osr              2
freq_osr              2
linear blocks         93
```

The implementation should derive dimensions from the configuration exactly as the FT8 monitor does.

Required behavior:

```text
query requirements
caller allocates aligned workspace
init
process 960-sample blocks
get waterfall view
reset window
reset stream
destroy
```

Normal slot/window transition semantics are not yet owned by T054; this task only needs the monitor lifecycle and a usable linear waterfall.

### FFT dependency

Use KissFFT as the MiniFT8 architecture does.

For T054, do **not** move or refactor the FT8 vendor directory. Since JS8Chat may not depend on FT8 internals, copy the minimal pinned KissFFT source/header set into a JS8-owned private vendor directory if needed:

```text
apps/js8chat/src/js8_engine/vendor/kissfft/
```

This temporary duplication is preferable to changing the accepted FT8 engine during initial JS8 bring-up.

Record the copied source provenance. A later post-M1 cleanup may deduplicate the FFT dependency once JS8 is proven.

## Candidate representation

Use a JS8-owned candidate type equivalent in shape to MiniFT8:

```c
typedef struct {
    int16_t score;
    int16_t time_offset;
    int16_t freq_offset;
    uint8_t time_sub;
    uint8_t freq_sub;
} Js8Candidate;
```

Do not reuse FT8 types or headers.

Suggested initial bounded policy, matching the existing MiniFT8 resource class:

```text
candidate capacity       50
minimum sync score        5
time search              -10 .. +19 blocks
```

These are initial implementation policy values, not frozen JS8 protocol facts. T055 may adjust them only with measured WAV evidence.

## Normal Costas search

JS8 Normal uses the original Costas sequence:

```text
4 2 5 6 1 3 0
```

at symbol offsets:

```text
0
36
72
```

Port the MiniFT8 candidate-search architecture and safe waterfall addressing, replacing only the Costas pattern.

The maximum cached score-term count remains 75 for this 3 x 7 sync structure.

Keep candidate search bounded and deterministic.

A synchronous full search API is sufficient for T054. A resumable search cursor may be ported now if doing so is a direct mechanical adaptation of the current FT8 code, but do not add scheduling/application ownership.

## Likelihood extraction

JS8 Normal channel tones are **direct binary 3-bit values**, not FT8 Gray mapped.

For each data tone, use the eight measured tone magnitudes directly:

```text
tone 0 = 000
tone 1 = 001
tone 2 = 010
tone 3 = 011
tone 4 = 100
tone 5 = 101
tone 6 = 110
tone 7 = 111
```

For max-log LLRs with T052 sign convention positive => bit 1:

```text
bit 0:
    max(4,5,6,7) - max(0,1,2,3)

bit 1:
    max(2,3,6,7) - max(0,1,4,5)

bit 2:
    max(1,3,5,7) - max(0,2,4,6)
```

Do not use the FT8 Gray table.

Data symbol locations are:

```text
7..35   -> LDPC codeword bits   0..86
43..71  -> LDPC codeword bits  87..173
```

Thus 58 data tones produce exactly 174 LLRs in the same parity-first/information-second ordering accepted by T052.

Use the same waterfall byte-to-dB interpretation and likelihood normalization structure as the current MiniFT8 decoder unless a test demonstrates a JS8-specific requirement:

```c
dB = byte * 0.5f - 120.0f
```

Do not add upstream v3.0.3 whitening, soft combining, frequency tracking, timing tracking, signal subtraction, or LDPC-feedback passes in T054. Those are desktop decoder enhancements, not required by the frozen MiniFT8-style architecture.

## CRC / decoded payload boundary

After BP success:

- T052 returns the recovered 87 information bits.
- `js8_crc12_check()` must pass.
- Return the first 75 bits as the validated JS8 payload.

Suggested output:

```c
typedef struct {
    Js8Candidate candidate;
    int ldpc_errors;
    uint8_t payload_bits[JS8_PAYLOAD_BITS];
} Js8DecodedPayload;
```

No text/application parsing occurs here.

Distinct decoder status values should distinguish at least:

```text
OK
invalid input
LDPC failure
CRC failure
```

## Synthetic end-to-end golden

Add a deterministic host regression that does **not** depend on the real WAV yet.

Use one checked-in T053 upstream-derived 79-tone vector directly as the transmitted symbol source; do not generate the test waveform through `js8_channel_encode()` alone.

Generate synthetic 6 kHz mono float audio with:

```text
base audio frequency    1000.0 Hz
tone spacing               6.25 Hz
symbol duration           160 ms
79 symbols
start near JS8 Normal's 500 ms nominal delay
no noise initially
bounded amplitude
```

Prefer a 500 ms start delay in the synthetic stream so timing search is exercised rather than perfectly block-aligned.

Feed the generated samples through `Js8Monitor` in exact 960-sample blocks, obtain the waterfall, search candidates, and attempt decode strongest-first until the expected payload is found.

Acceptance identity is:

```text
exact 75 payload bits == chosen pinned T053 vector
CRC passes
```

Do not assert a permanent exact candidate score/order unless necessary for a local unit invariant.

The test should prove at least one valid candidate decode and exact payload recovery.

## Monitor tests

Add focused tests for:

- requirements/dimensions;
- insufficient workspace;
- misaligned workspace;
- reset-window/reset-stream semantics;
- two independent instances;
- waterfall-full status;
- safe destroy;
- no heap.

Do not require an FT8 waterfall fingerprint. JS8 owns its own monitor boundary even though the mathematics is intentionally equivalent.

## Decoder tests

Add focused tests for:

- invalid waterfall/candidate arguments;
- capacity enforcement;
- minimum-score pruning;
- exact Normal Costas groups;
- direct binary likelihood mapping (no Gray);
- LDPC failure status;
- CRC failure status where constructible;
- synthetic 6 kHz end-to-end payload recovery.

## Resource visibility

At test time print or document the baseline host monitor requirements:

```text
total workspace
waterfall bytes
FFT plan bytes
window/history/scratch bytes
block stride
num bins
```

Do not optimize based on x86 sizes yet.

T054 should demonstrate that the architecture remains in the MiniFT8 resource class. ADV optimization is later.

## Build / architecture enforcement

Extend the existing JS8Chat architecture rule only to recognize private vendor headers under the same `js8_engine` ownership.

Do not permit dependencies on `apps/ft8`.

The JS8 engine remains a no-heap module.

## Non-goals

Do not implement:

- real `A_2_1.wav` decode;
- WAV parsing;
- 12 kHz input frontend/decimator;
- Js8Engine top-level lifecycle wrapper;
- UTC slot framer;
- application/message parsing;
- 12-character text codec;
- callsigns;
- CQ/HB/directed commands;
- Huffman/JSC;
- conversations;
- UI;
- TX scheduling;
- MiniShell app registration;
- live UAC;
- QMX CAT;
- ADV performance tuning;
- other JS8 speeds;
- generic shared FT8/JS8 DSP refactor.

## Acceptance criteria

- [x] JS8-owned 6 kHz/960-sample monitor implemented with caller-owned workspace.
- [x] monitor uses MiniFT8 compact-waterfall architecture, not upstream desktop raw PCM architecture.
- [x] Normal Costas candidate search uses exactly `4 2 5 6 1 3 0`.
- [x] sync groups are evaluated at symbols 0, 36, and 72.
- [x] likelihood extraction uses direct binary tone mapping; no FT8 Gray map.
- [x] 174 LLRs preserve parity-first/info-second codeword order.
- [x] T052 LDPC decoder is reused.
- [x] T052 CRC-12 checker is reused.
- [x] successful decoder output is exact 75-bit payload.
- [x] deterministic synthetic 6 kHz waveform from a pinned T053 tone vector decodes back to the exact payload.
- [x] bounded candidate capacity; no heap.
- [x] no mutable DSP singleton.
- [x] no FT8 production code changes.
- [x] no MiniShell/platform/application dependency.
- [x] Linux full CTest passes.
- [x] portable CTest passes.
- [x] JS8 boundary checks pass.
- [x] ASan/UBSan pure JS8 regression passes.
- [x] real ADV build remains green.
- [x] `git diff --check` passes.
- [x] no unrelated cleanup.

No manual/hardware validation is required.

## Automated tests

Run:

```bash
git status --short

cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure

cmake -S tests/unit -B /tmp/T054-build-unit
cmake --build /tmp/T054-build-unit -j"$(nproc)"
ctest --test-dir /tmp/T054-build-unit --output-on-failure

PYTHONDONTWRITEBYTECODE=1 python3 tests/app_dependency_boundary.py . js8chat
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_platform_boundary.py . js8chat

source ~/projects/esp-idf/export.sh
idf.py -C platform/adv build

# Run the task-specific synthetic JS8 RX test directly if it has a standalone target.

# ASan/UBSan: compile/run the pure JS8 PHY/monitor/decoder test target with the
# same sanitizer policy used by T052/T053. Include the private JS8 KissFFT
# sources when required.

git diff --check
```

## Branch workflow

Use:

```text
codex/T054-js8-monitor-decoder
```

Codex:

1. read AGENTS.md, canonical JS8 docs, T052/T053, and the current FT8 monitor/decoder architecture;
2. implement a JS8-owned monitor and payload decoder without modifying FT8;
3. keep Normal mode hard-coded and explicit;
4. add the synthetic upstream-tone-vector end-to-end regression;
5. run all required local gates;
6. set Status to REVIEW;
7. fill implementation notes including measured workspace sizes;
8. commit and push one reviewable commit;
9. return commit SHA;
10. no PR;
11. no GitHub Actions wait.

## Codex implementation notes

### Implementation summary

Implemented the JS8-owned 6 kHz/960-sample monitor, synchronous Normal Costas
candidate search, direct-binary max-log likelihood extraction, and T052
LDPC/CRC payload validation. Monitor window/FFT arithmetic, dimensions and
candidate heap policy are adapted from the current MiniFT8 source at `e3f6d80`.

The synthetic test streams 93 blocks of bounded, continuous-phase PCM from
the third pinned T053 upstream tone vector, at base 1000 Hz, spacing 6.25 Hz,
start sample 3000 (500 ms), amplitude 0.5. Search and decoding recover its exact
75 payload bits with zero hard errors. No production channel encode call or
external audio fixture is used to generate the waveform.

### Files changed

- `apps/js8chat/src/js8_engine/js8_monitor.[ch]`: queryable aligned workspace,
  compact linear waterfall, block processing, reset and destroy lifecycle.
- `apps/js8chat/src/js8_engine/js8_decoder.[ch]`: JS8 types/statuses, bounded
  synchronous search, raw likelihood diagnostic edge and validated payload API.
- `apps/js8chat/src/js8_engine/vendor/kissfft/`: five private source/header
  files copied from MiniFT8, with source hashes and no-heap changes in README.
- `apps/js8chat/src/js8_engine/README.md`: implemented RX ownership and semantics.
- `tests/js8_rx_test.c`: monitor, mapping/search/error tests and synthetic RX.
- `tests/js8_no_heap_test.py`: compiled-library heap dependency check.
- `tests/js8_tests.cmake`: pure RX library and regression targets shared by
  Linux and portable suites.
- `tests/architecture_rules.py`: only adds private vendor include-root lookup
  under existing js8_engine ownership; no permission or no-heap relaxation.
- This task packet: REVIEW handoff, test evidence and resource measurements.

### Invariants preserved

Pure C, fixed/caller-owned storage, no heap or mutable DSP singleton. Normal
geometry only. No FT8 source modifications, dependencies, generic DSP refactor,
MiniShell/platform APIs, raw-slot PCM capture, application/UTC ownership, WAV
use, or JS8Call desktop receive enhancements. T052/T053 product code and golden
vectors remain unchanged.

The monitor follows current source reset semantics: reset-window clears count,
diagnostics and FFT history while retaining waterfall bytes; reset-stream also
clears waterfall bytes. Old views are invalid after reset/processing. The JS8
view uses an allocation-head pointer plus a logical first-block label rather
than FT8's biased UTC-origin pointer. Negative logical rows are safely indexed
and tested; no UTC-specific data-erasure/overwrite rule is carried over.

Synchronous direct scoring retains the same 75 maximum neighborhood comparisons
and bounded 50-candidate policy without introducing score-cache/scheduling state.
Data rows 7..35 and 43..71 produce 174 LLRs with positive meaning bit 1, then
MiniFT8 variance normalization and T052 BP/CRC. Invalid inputs, LDPC failure and
CRC failure are distinct; failed attempts do not expose unvalidated payload.
Flat/singular likelihood variance returns LDPC failure without dividing by zero.

The private FFT accepts size queries/caller workspace only, float forward real
transforms, out-of-place operation and factors 2/3/5. Generic-radix/in-place
heap scratch, unused inverse-real code and OpenMP paths were removed. Private
exported functions are prefixed to avoid future collisions with the FT8 copy.
OSRs fit uint8 lanes, time_osr divides 960, frequency OSR must be FFT-supported;
frequency bounds and samples are validated before processing. The normalized
PCM contract is finite samples in [-1, 1]. These are local primitive validation
choices, not mode or architecture changes. No scope deviations.

### Local tests run

```sh
git status --short
cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure
# PASS: 93/93, including existing FT8 and architecture/platform regressions.

cmake -S tests/unit -B /tmp/T054-build-unit
cmake --build /tmp/T054-build-unit -j"$(nproc)"
ctest --test-dir /tmp/T054-build-unit --output-on-failure
# PASS: 17/17.

PYTHONDONTWRITEBYTECODE=1 python3 tests/app_dependency_boundary.py . js8chat
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_platform_boundary.py . js8chat
# Both PASS.

source ~/projects/esp-idf/export.sh
idf.py -C platform/adv build
# PASS: minishell_adv.bin 0x151790 bytes, app partition 78% free.

./build-linux/js8_rx_unit
# PASS: exact pinned payload, CRC gate passed, hard_errors=0.
# Diagnostic only: 50 candidates, matching rank 0, score 34,
# time_offset=4/time_sub=0, freq_offset=128/freq_sub=0.

cmake -S tests/unit -B /tmp/T054-build-sanitize \
  -DCMAKE_C_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer -g'
cmake --build /tmp/T054-build-sanitize --target js8_rx_unit js8_phy_unit -j"$(nproc)"
ctest --test-dir /tmp/T054-build-sanitize -R '^js8_(rx|phy)_unit$' --output-on-failure
# PASS: 2/2, with private FFT sources instrumented too.

git diff --check
# PASS.
git diff -- apps/ft8
# Empty: no FT8 production changes.
```

Sanitizer execution used approved escalation because the prior tasks established
LeakSanitizer's incompatibility with the sandbox's ptrace environment. All tests
passed; no existing assertions were weakened. The no-heap regression uses `nm -u`
on both JS8 archives and finds no heap symbols, including hidden vendor calls.

### Measured resource data

Baseline host GCC/x86-64, time_osr=2/freq_osr=2, 200..2900 Hz:

| Resource | Bytes / value |
| --- | ---: |
| Total aligned workspace | 211312 |
| Waterfall | 161076 |
| FFT plan | 19488 |
| Window | 7680 |
| Rolling FFT history | 7680 |
| Time scratch | 7680 |
| Frequency scratch | 7688 |
| Alignment/padding within total | 20 |
| Required alignment | 16 |
| Block stride | 1732 |
| Bins | 433 (min 32, exclusive max 465) |
| Blocks / samples per block | 93 / 960 |
| FFT length / samples per time subblock | 1920 / 480 |
| `sizeof(Js8Monitor)` | 200 |
| `sizeof(Js8Candidate)` / 50 candidates | 8 / 400 |
| `sizeof(Js8DecodedPayload)` | 88 |

The runtime query/test prints these measurements. A separate temporary helper
compiled the unchanged FT8 monitor and its FFT copy with the same baseline:
total 211312, waterfall 161076, FFT plan 19488, `sizeof(Ft8Monitor)` 208. Thus
the monitor workspace is exactly the current MiniFT8 resource class.

Additional host compiler stack visibility (`cc -std=c11 -O2 -fstack-usage` for
js8_monitor.c, js8_decoder.c and js8_ldpc.c, with local engine/vendor include
paths, object outputs under `/tmp/T054-stack`): monitor process frame 80 bytes,
search frame 184, likelihood frame 72, candidate decode frame 1024, T052 LDPC
frame 5392. These are individual compiler-reported frames, not complete call
chain/high-water measurements; FFT recursion, libm and caller stack are extra.
No ADV optimization or x86-size tuning was performed.

### Manual/hardware validation still required

None for T054. No flashing, RF validation or real WAV use. ADV gate builds the
existing firmware; JS8 application registration/target integration remain later
work. The architect's `A_2_1.wav` is untouched and reserved for T055.

### Known limitations / risks

Only the deterministic noiseless synthetic signal is RX evidence here; real
WAV timing/frequency/noise sensitivity remains unmeasured. Candidate policy may
need measured T055 adjustments. Candidate time offsets include causal FFT
history delay and are not UTC start-time estimates. T052's un-clipped BP numeric
behavior is unchanged. Workspace sizes/alignment and stack frames are host
measurements and must be queried/measured again on ADV.

Pre-existing untracked build scripts, keyer ELF-app artifacts and Python caches
were left outside the change.

### Commit

The reviewed implementation commit is:

`0486cb3c474505eeb3601247abc5176c6f522338`

No PR or GitHub Actions wait.

## Supervisor review

Reviewed commit `0486cb3c474505eeb3601247abc5176c6f522338` against T054.

Result: **PASS**.

Review findings:

- One bounded implementation commit, one commit ahead of the T054 baseline.
- FT8 production source is unchanged; JS8 owns its own monitor, decoder, and private FFT copy.
- Synthetic RX starts from the checked-in upstream T053 tone vector, not from `js8_channel_encode()`, so it independently exercises PCM synthesis -> JS8 monitor -> candidate search -> likelihood extraction -> T052 LDPC/CRC -> exact 75-bit payload.
- Normal Costas search is exactly `4 2 5 6 1 3 0` at 0/36/72.
- Data LLR extraction is direct-binary, not FT8 Gray mapped, and preserves parity-first/information-second ordering.
- Candidate storage is bounded to 50 and the search uses safe in-allocation logical indexing.
- Decode returns payload only after successful LDPC and CRC; invalid/LDPC/CRC outcomes remain distinct.
- Monitor state is caller-owned and queryable; no full-slot PCM buffer or mutable DSP singleton exists.
- Compiled no-heap checks cover both the JS8 PHY and RX archives, including the private FFT.
- Private KissFFT changes are confined to the JS8 copy, documented with source provenance, preserve the forward float FFT path used by 960/1920 geometry, and remove heap-only/unused paths rather than altering the accepted FT8 copy.
- Reported host workspace is 211,312 bytes at the 2x2 baseline, exactly matching the current MiniFT8 monitor resource class. JS8 monitor instance is slightly smaller (200 vs 208 bytes).
- Reported stack frames are bounded; the largest listed JS8 frame is T052 LDPC at 5,392 bytes. ADV high-water remains a later integration measurement.
- Linux 93/93, portable 17/17, boundary checks, no-heap regression, ASan/UBSan, ADV build, and diff checks all pass.
- Real WAV reception and sensitivity remain intentionally unproven and move to T055.

Main was fast-forwarded to the reviewed implementation commit.

## Architect test result

No manual/hardware validation is required for T054. Software review accepted; task complete.

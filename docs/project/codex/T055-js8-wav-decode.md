# T055 — Linux JS8 Normal WAV decoder

Status: COMPLETE

## Architect intent

Reach the first real JS8Chat receive milestone using the exact upstream JS8Call-improved Normal-mode test WAV already available locally at:

    ~/projects/js8chat/A_2_1.wav

Use the same flow that proved MiniFT8: host WAV first, live MiniShell/ADV later.

JS8Chat remains KISS and supports JS8 Normal / Mode A only.

## Objective

Implement a standalone Linux utility that performs:

    12 kHz mono S16 WAV
      -> normalize + simple 2:1 decimation
      -> 6 kHz / 960-sample blocks
      -> T054 monitor
      -> Normal Costas candidate search
      -> T054 LDPC/CRC payload decode
      -> unique 75-bit payload(s)
      -> minimal physical-frame view
      -> stdout

The utility target is apps/js8chat/tools/js8_decode.c.

T055 must prove the accepted T054 engine decodes a real upstream waveform. Do not expand into the full JS8 application/chat protocol.

## Accepted baseline

T052 CRC-12 + LDPC(174,87): COMPLETE.
T053 Normal 79-tone channel map: COMPLETE.
T054 monitor + payload decoder: COMPLETE at 0486cb3c474505eeb3601247abc5176c6f522338.

Read AGENTS.md, docs/js8/*, T052-T054, apps/js8chat/src/js8_engine/README.md, and the current MiniFT8 host decoder only as an architecture example.

## Exact fixture identity

Frozen upstream reference:

- repository: JS8Call-improved/JS8Call-improved
- tag: v3.0.3
- path: media/tests/A_2_1.wav
- Git blob SHA: d986a4e5a9cc654dffbfadae73ec35cc9cea1d83
- size: 360208 bytes

The upstream media/tests README defines names as {MODE}_{DEPTH}_{EXPECTED_DECODES}.wav, therefore A_2_1 means Normal mode, upstream depth 2, expected decode count 1.

Before using the local file, run:

    git hash-object ~/projects/js8chat/A_2_1.wav

It must equal d986a4e5a9cc654dffbfadae73ec35cc9cea1d83.

If it does not match, mark T055 BLOCKED and report the mismatch. Do not silently use a different fixture.

## Architectural constraints

- Normal/Mode A only.
- First run the accepted T054 DSP unchanged against the real fixture.
- Do not change search/sensitivity policy until failure evidence is collected.
- Any T054 policy change must be narrowly justified and recorded.
- No upstream desktop decoder architecture, whitening, subtraction, soft-combining, or generic submodes.
- No FT8 production-source changes.
- js8_engine remains pure, no-heap, platform-independent.
- Host utility may use normal Linux/POSIX file/allocation APIs.
- No UI, MiniShell app registration, live UAC, QMX CAT, conversations, JSC, or full JS8 application protocol.

## WAV input and frontend

Required WAV contract:

- RIFF/WAVE
- PCM format 1
- mono
- 12000 samples/s
- 16-bit little-endian

Parse RIFF chunks safely; do not assume a fixed 44-byte header.

Normalize S16 to float and use the same simple 2:1 phase-preserving decimation style as MiniFT8. Do not add an FIR/resampler unless the unchanged path is proven insufficient.

A 15-second file yields 90,000 samples at 6 kHz:

- 93 complete 960-sample blocks = 89,280 samples
- remainder = 720 samples

Process exactly the 93 complete blocks. Ignore the final incomplete 720-sample engine block; do not pad and do not create a 94th block.

## Candidate/decode flow

After capture:

1. obtain the T054 waterfall;
2. run the accepted T054 baseline first: capacity 50, min score 5, time search -10..+19, time_osr 2, freq_osr 2;
3. try candidates strongest-first;
4. retain only LDPC+CRC-valid payloads;
5. deduplicate by exact 75-bit payload;
6. print all unique valid payloads.

Hard fixture acceptance:

    unique valid payload count == 1

Candidate score/order are diagnostics, not identity.

If unchanged T054 does not decode one payload, record candidate count, top scores, time/frequency lattice, LDPC failures and CRC failures before changing anything.

## Minimal physical-frame helper

Add the smallest pure helper under js8_engine, for example js8_frame.[ch].

A 75-bit physical payload is:

- bits 0..71: 12 x 6-bit alphabet words
- bits 72..74: 3-bit frame type

Exact v3.0.3 alphabet:

    0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz-+

Expose an output equivalent to:

    char text12[13]
    uint8_t type

This is not the JS8 application-protocol parser. It only exposes the physical frame already present in the PHY.

Validate the helper against existing T052/T053 golden payloads.

## Utility output

Usage:

    ./build-linux/js8_decode ~/projects/js8chat/A_2_1.wav

For each unique valid decode print one stable line containing at least:

    payload=<75 bits> type=<0..7> frame="<12 chars>"

Score/time/frequency diagnostics may also be printed.

If frequency is printed, derive it as:

    200 + freq_offset*6.25 + freq_sub*(6.25/freq_osr)

For baseline freq_osr=2, freq_sub spacing is 3.125 Hz.

Do not label candidate time as exact UTC DT yet.

## External reference test

Do not commit the WAV into MiniShell.

Add an optional CMake cache path such as JS8_A2_1_REFERENCE_WAV.

When set, build/register a reference test invoking js8_decode and require exactly one unique valid decode.

Normal CTest must remain runnable without the external WAV.

Codex local validation must configure a separate reference build using $HOME/projects/js8chat/A_2_1.wav.

## First successful real-WAV evidence

Record in this task:

- fixture Git blob hash
- unique decode count
- exact 75-bit payload
- physical frame type
- 12-character physical frame
- candidate score
- candidate time lattice
- candidate frequency lattice
- derived audio frequency
- LDPC hard-error count

These become the first real-WAV regression evidence.

## Architecture enforcement

Extend JS8Chat rules with a tools module analogous to FT8:

    tools -> tools + js8_engine

Permit fopen only for the exact host utility path as a native exception.

Do not relax js8_engine no-heap or platform rules.

## Acceptance criteria

- [x] local fixture blob matches pinned v3.0.3 blob
- [x] js8_decode accepts the real 12 kHz mono S16 WAV
- [x] frontend produces continuous 6 kHz samples
- [x] exactly 93 full monitor blocks are processed
- [x] unchanged T054 policy is tried first
- [x] exactly one unique valid 75-bit payload is recovered
- [x] payload is printed stably
- [x] physical type and 12-character frame are printed
- [x] first-run diagnostics are recorded
- [x] external WAV is not committed
- [x] normal tests require no external WAV
- [x] optional pinned-WAV reference test passes
- [x] js8_engine remains pure/no-heap
- [x] FT8 production source unchanged
- [x] Linux full CTest passes
- [x] portable CTest passes
- [x] JS8 boundary checks pass
- [x] ASan/UBSan passes
- [x] ADV build remains green
- [x] git diff --check passes
- [x] no unrelated cleanup

No RF/hardware validation is required. After supervisor review, the architect will manually run js8_decode on pc-1; that manual run is the milestone acceptance.

## Required local commands

Run at least:

    git hash-object ~/projects/js8chat/A_2_1.wav

    cmake -S . -B build-linux
    cmake --build build-linux -j"$(nproc)"
    PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure

    cmake -S tests/unit -B /tmp/T055-build-unit
    cmake --build /tmp/T055-build-unit -j"$(nproc)"
    ctest --test-dir /tmp/T055-build-unit --output-on-failure

    PYTHONDONTWRITEBYTECODE=1 python3 tests/app_dependency_boundary.py . js8chat
    PYTHONDONTWRITEBYTECODE=1 python3 tests/app_platform_boundary.py . js8chat

    cmake -S . -B /tmp/T055-build-ref -DJS8_A2_1_REFERENCE_WAV="$HOME/projects/js8chat/A_2_1.wav"
    cmake --build /tmp/T055-build-ref -j"$(nproc)"
    ctest --test-dir /tmp/T055-build-ref -R "js8.*reference|js8.*A2.*1" --output-on-failure
    /tmp/T055-build-ref/js8_decode "$HOME/projects/js8chat/A_2_1.wav"

    source ~/projects/esp-idf/export.sh
    idf.py -C platform/adv build

    git diff --check
    git diff -- apps/ft8

The final FT8 diff must be empty.

Run ASan/UBSan on the pure JS8 tests and, if practical, the host reference decode using the same sanitizer policy as T054.

## Branch workflow

Use branch:

    codex/T055-js8-wav-decode

Codex must:

1. verify fixture identity before coding against it;
2. implement the physical-frame helper and Linux WAV decoder;
3. try unchanged T054 first;
4. collect evidence before any policy change;
5. record the exact successful real-WAV result;
6. add optional reference-test integration without checking in the WAV;
7. run all gates;
8. set Status to REVIEW;
9. fill implementation notes;
10. commit and push one reviewable commit;
11. return the SHA;
12. no PR and no Actions wait.

## Codex implementation notes

### Implementation summary

Added the Linux `js8_decode` utility and the pure `js8_frame_unpack()` physical
frame helper. The utility parses RIFF chunks, normalizes S16 samples and keeps
the continuous phase-0 sequence (input samples 0, 2, 4, ...), passes complete
960-sample blocks to the accepted T054 monitor, searches strongest-first,
validates through T054/T052 LDPC+CRC, and deduplicates by all 75 payload bits.
Stdout contains one stable line per unique payload; stderr contains top-five
candidate diagnostics and capture/decode counts.

The accepted T054 DSP and policy decoded the real fixture unchanged. No search,
normalization, sensitivity, FFT, BP, CRC or monitor changes were necessary.

### Files changed

- `apps/js8chat/tools/js8_decode.c`: bounded single-window host reader/decoder.
- `apps/js8chat/src/js8_engine/js8_frame.[ch]`: twelve 6-bit alphabet words plus
  three-bit type, with validation and no application-protocol parsing.
- `tests/js8_frame_test.c`: three existing golden payloads, all 64 alphabet
  values and eight types, null/invalid arguments and output bounds.
- `tests/js8_wav_test.py`: synthetic upstream-tone waveform, phase-0 decimation,
  exact 93-block capture/tail handling, nonstandard chunk order, odd padding,
  extended fmt, malformed/truncated headers/data, format and length rejection.
- `tests/js8_wav_reference.py`: optional external fixture blob/size verification
  and exactly one expected payload/type/physical-frame regression.
- `CMakeLists.txt`, `tests/js8_tests.cmake`: host utility/tests and pure frame test;
  optional `JS8_A2_1_REFERENCE_WAV` cache path (unset in normal builds).
- `tests/architecture_rules.py`: tools -> tools + js8_engine; fopen exception
  only at `tools/js8_decode.c`. Engine purity/no-heap restrictions unchanged.
- This task packet: REVIEW status and exact evidence.

### Invariants preserved

Normal only, 6 kHz/960-sample engine blocks, time_osr=2/freq_osr=2, capacity 50,
minimum score 5, -10..19 time search, positive LLR means 1 and parity-first
codeword order. The real 180000-sample input yields 90000 engine samples;
exactly 93 complete blocks are processed, with 720 remaining engine samples
ignored. No padding or 94th block. Inputs exceeding 93 complete blocks are
rejected rather than silently truncated into one window.

Engine remains pure C/no-heap, with unchanged T052-T054 product source and
private FFT. Only the host utility owns file access and workspace allocation.
Full RIFF/chunk bounds are checked, including the ignored tail; no fixed
44-byte header assumption or full-file PCM allocation. Physical frame decoding
is not application message interpretation. No FT8 changes, WAV commit, live
platform integration, FIR/resampler or broader protocol work. No deviations
from task scope.

### First real-WAV decode evidence

The very first command before implementation was:

```sh
git hash-object ~/projects/js8chat/A_2_1.wav
# d986a4e5a9cc654dffbfadae73ec35cc9cea1d83
```

File size: 360208 bytes. Source identity: JS8Call-improved v3.0.3,
`media/tests/A_2_1.wav`. This identity is also enforced by the optional reference
test using the Git blob hash construction.

The initial host-reader probe rejected the container before invoking DSP:
RIFF declares 360199 bytes (end offset 360207), while the final LIST chunk is
155 bytes plus its physical padding byte at offset 360207. Its data chunk is
360000 bytes at offset 44. The reader now accepts a final odd-chunk pad outside
the declared RIFF end only when that byte exists in the file; chunk contents
must still fit within RIFF. A focused positive/negative regression covers this
upstream convention. This was solely a container-reader correction.

The **first DSP run** then used the accepted T054 path unchanged and succeeded:

```text
payload=111001011101001010000111001011100000101011000001100010000111111111111111010 type=2 frame="vTA7BWh1Y7++" score=26 time=5/0 freq=57/0 hz=556.250 hard_errors=15
```

Exact first-run stderr:

```text
candidate=0 score=26 time=5/0 freq=57/0 status=0
candidate=1 score=14 time=5/0 freq=57/1 status=-2
candidate=2 score=12 time=18/0 freq=54/0 status=-2
candidate=3 score=10 time=5/0 freq=56/1 status=-2
candidate=4 score=10 time=17/0 freq=110/1 status=-2
blocks=93 ignored_engine_samples=720 candidates=50 ldpc_fail=49 crc_fail=0 valid=1 unique=1
```

Thus unique LDPC+CRC-valid payload count is exactly **1**, physical type **2**,
physical frame **`vTA7BWh1Y7++`**, score **26**, time lattice **5/0**, frequency
lattice **57/0**, audio frequency **556.250 Hz**, LDPC hard-error count **15**.
Frequency is `200 + 57*6.25 + 0*3.125`. Time is a candidate lattice coordinate,
not exact UTC DT. Scores/order/hard-error counts are recorded diagnostics;
the reference test locks payload, type, frame, unique count and block handling.
Separate reference and sanitized builds reproduce the same payload/frame.

### Local tests run

All final gates passed:

```sh
git hash-object ~/projects/js8chat/A_2_1.wav
# PASS: d986a4e5a9cc654dffbfadae73ec35cc9cea1d83

cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure
# PASS: 95/95, no external fixture configured.

cmake -S tests/unit -B /tmp/T055-build-unit
cmake --build /tmp/T055-build-unit -j"$(nproc)"
ctest --test-dir /tmp/T055-build-unit --output-on-failure
# PASS: 18/18, including pure frame helper and compiled no-heap regression.

PYTHONDONTWRITEBYTECODE=1 python3 tests/app_dependency_boundary.py . js8chat
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_platform_boundary.py . js8chat
# Both PASS.

cmake -S . -B /tmp/T055-build-ref \
  -DJS8_A2_1_REFERENCE_WAV="$HOME/projects/js8chat/A_2_1.wav"
cmake --build /tmp/T055-build-ref -j"$(nproc)"
ctest --test-dir /tmp/T055-build-ref -R 'js8.*reference|js8.*A2.*1' --output-on-failure
/tmp/T055-build-ref/js8_decode "$HOME/projects/js8chat/A_2_1.wav"
# PASS: 1/1 reference test; exact single payload line and diagnostics above.

source ~/projects/esp-idf/export.sh
idf.py -C platform/adv build
# PASS: minishell_adv.bin 0x151790 bytes; app partition 78% free.

cmake -S . -B /tmp/T055-build-sanitize \
  -DCMAKE_C_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer -g' \
  -DJS8_A2_1_REFERENCE_WAV="$HOME/projects/js8chat/A_2_1.wav"
cmake --build /tmp/T055-build-sanitize \
  --target js8_rx_unit js8_phy_unit js8_frame_unit js8_decode -j"$(nproc)"
ctest --test-dir /tmp/T055-build-sanitize \
  -R '^js8_(phy_unit|rx_unit|frame_unit|wav_unit|A2_1_reference)$' --output-on-failure
# PASS: 5/5, pure tests plus malformed/synthetic and real host-WAV paths.

git diff --check
# PASS.
git diff -- apps/ft8
# Empty.
git diff -- apps/js8chat/src/js8_engine/js8_monitor.c apps/js8chat/src/js8_engine/js8_decoder.c
# Empty: unchanged T054 DSP/policy.
```

Sanitizer execution used approved escalation because prior tasks established
LeakSanitizer cannot execute inside the ptrace-based sandbox. No test assertions
were weakened. Existing unrelated untracked scripts, old keyer artifacts and
Python caches were excluded.

### Manual validation still required

After supervisor review, the architect runs:

```sh
./build-linux/js8_decode ~/projects/js8chat/A_2_1.wav
```

on pc-1 for milestone acceptance. That manual result remains pending; no RF or
hardware test is required. ADV build here validates existing firmware only.

### Known limitations / risks

This is one bounded Normal receive window from a seekable mono 12 kHz PCM16
RIFF file. No multi-slot utility, anti-alias filter, live frontend, application
message parsing or protocol meaning is claimed for the twelve physical
characters. Simple decimation and the existing T054 policy are proven for this
pinned fixture, not a broad sensitivity corpus. The general utility can report
zero or multiple decodes; exactly one is the pinned reference-test gate.

### Commit

The reviewed implementation commit is:

`62d474ea878fb147b9c5412cf6f60b926b75d5d1`

No PR or GitHub Actions wait.

## Supervisor review

Reviewed commit `62d474ea878fb147b9c5412cf6f60b926b75d5d1` against T055.

Result: **PASS — implementation accepted; manual pc-1 acceptance pending.**

Review findings:

- One bounded implementation commit, one commit ahead of the T055 baseline.
- The pinned local fixture identity was verified before DSP work: Git blob `d986a4e5a9cc654dffbfadae73ec35cc9cea1d83`, matching JS8Call-improved v3.0.3 `media/tests/A_2_1.wav`.
- The accepted T054 monitor/search/LLR/LDPC/CRC product source is unchanged. The first real DSP run succeeded without search/sensitivity-policy changes.
- The utility safely parses RIFF chunks instead of assuming a 44-byte header and includes a narrow compatibility case for the pinned upstream file's final odd LIST padding convention.
- The host frontend keeps the continuous phase-0 2:1 decimation sequence and processes exactly 93 complete 960-sample blocks; the remaining 720 engine samples are ignored without padding.
- Candidate decoding retains only LDPC+CRC-valid payloads and deduplicates by exact 75-bit payload.
- Exactly one unique valid payload is recovered from the pinned fixture:
  `111001011101001010000111001011100000101011000001100010000111111111111111010`.
- Independent supervisor unpacking of that 75-bit payload with the pinned v3.0.3 alphabet gives physical frame `vTA7BWh1Y7++` and type `2`, matching the utility.
- First-run diagnostics are coherent: score 26, lattice time 5/0, lattice frequency 57/0, derived audio frequency 556.250 Hz, hard-errors 15.
- The new `js8_frame` helper remains a pure physical-frame unpacker; it does not interpret JS8 application commands/messages.
- The external WAV is not committed. Normal CTest is fixture-independent; the optional reference build pins the external fixture by Git blob and exact expected payload/type/frame.
- JS8 architecture rules add only a `tools` owner with `tools -> tools + js8_engine`; engine purity/no-heap remains unchanged.
- FT8 production source is unchanged.
- Reported gates are consistent with the diff: Linux 95/95, portable 18/18, external reference, malformed/synthetic WAV tests, ASan/UBSan, boundary checks, ADV build, and diff checks all pass.

Main was fast-forwarded to the reviewed implementation commit.

T055 now enters TESTING for the architect's manual pc-1 run.

## Architect test result

**PASS on pc-1.**

Architect manually ran:

```sh
./build-linux/js8_decode ~/projects/js8chat/A_2_1.wav
```

Observed:

```text
candidate=0 score=26 time=5/0 freq=57/0 status=0
payload=111001011101001010000111001011100000101011000001100010000111111111111111010 type=2 frame="vTA7BWh1Y7++" score=26 time=5/0 freq=57/0 hz=556.250 hard_errors=15
candidate=1 score=14 time=5/0 freq=57/1 status=-2
candidate=2 score=12 time=18/0 freq=54/0 status=-2
candidate=3 score=10 time=5/0 freq=56/1 status=-2
candidate=4 score=10 time=17/0 freq=110/1 status=-2
blocks=93 ignored_engine_samples=720 candidates=50 ldpc_fail=49 crc_fail=0 valid=1 unique=1
```

This exactly matches the reviewed reference evidence: one unique LDPC+CRC-valid payload, type 2, physical frame `vTA7BWh1Y7++`, score 26, lattice frequency 57/0 (556.250 Hz), and 15 hard errors.

T055 and the first Linux real-WAV JS8 Normal decode milestone are complete.

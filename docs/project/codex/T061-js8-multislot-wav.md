# T061 — Aligned multi-slot JS8 WAV decoder

Status: REVIEW

## Architect intent

Make the Linux host decoder useful for aligned 1-2 minute WebSDR recordings without changing the proven JS8 engine.

The architect will ensure WAV PCM sample 0 is aligned to a real 15-second JS8 Normal boundary.

Therefore slot alignment is exact sample arithmetic, not a search heuristic:

    12000 samples/s * 15 s = 180000 input samples per slot

No overlapping windows, wall clock, UTC parser, or timing inference is needed.

This task is host-tool work only. FIRST/LAST directed-message reassembly remains T062.

## Objective

Extend apps/js8chat/tools/js8_decode.c with an explicit aligned multi-slot mode:

    ./build-linux/js8_decode --all-slots capture.wav

The existing invocation:

    ./build-linux/js8_decode capture.wav

must preserve the accepted T060 first-window behavior and stable one-slot output.

--all-slots decodes every complete aligned 15-second slot in the WAV.

## Exact slot geometry

Input WAV remains mono S16 PCM at 12000 samples/s.

One JS8 Normal slot is exactly:

    15 s
    180000 input samples @ 12 kHz
    360000 PCM data bytes

The current 6 kHz monitor window is:

    93 blocks * 960 samples = 89280 engine samples
    = 178560 input samples @ 12 kHz
    = 14.88 s

Therefore for each aligned slot:

    slot start input sample = slot_index * 180000

    samples [0 .. 178559] relative to slot
        -> phase-0 2:1 decimation
        -> 89280 engine samples
        -> 93 * 960 monitor blocks
        -> decode

    samples [178560 .. 179999]
        -> 1440 input samples
        -> deliberately skipped

Then the next slot begins at exactly the next 180000-sample boundary.

Because 180000 is even, the phase-0 2:1 decimator has the same phase at every slot boundary.

## Alignment contract

--all-slots assumes PCM data sample 0 is an exact JS8 15-second boundary.

Do not attempt to detect or correct a misaligned recording in T061.
Do not use candidate timing to shift slot boundaries.
Do not overlap slots.
Do not search every 5 seconds.

## Slot count

For --all-slots:

    full_slots = total_input_samples / 180000
    trailing_input_samples = total_input_samples % 180000

Require at least one complete 15-second slot.
Decode exactly full_slots.
Ignore trailing_input_samples after the last full slot.
Do not decode a partial final slot in T061.

The existing no-option first-window mode retains its current behavior for short/non-slot-sized WAV fixtures.

## Host implementation shape

Refactor host-only code as needed so one function can decode one slot/window while reusing the accepted monitor/decoder primitives.

Preferred ownership:

    RIFF parser
      -> knows data offset + sample count

    decode_slot(slot_index)
      -> seek to exact PCM slot sample offset
      -> reset monitor stream
      -> process 93 blocks
      -> candidate search/decode
      -> per-slot payload dedupe
      -> print slot-tagged results

Initialize/query/allocate the monitor workspace once and reuse it across slots.
Between aligned slots use js8_monitor_reset_stream() or equivalent accepted lifecycle reset.
Do not allocate a new 211 kB monitor workspace per slot.
JSC dictionary state/file may also be reused lazily across all slots.

## Raw input access

The HostWav abstraction may be extended to retain data_offset and total_samples.

For each slot, seek to:

    data_offset + slot_index * 180000 * 2 bytes

Then read only the 178560 input samples needed for DSP.
It is not necessary to physically read the final 1440 input samples of a slot; seeking to the next exact boundary is sufficient.

The complete RIFF/container must still be validated before DSP, exactly as T059/T060 require.
Malformed/truncated data later in the RIFF must not become invisible merely because DSP uses slot seeks.

## Decimation

Preserve exactly the accepted frontend:

    take input samples 0,2,4,... within each aligned slot

Since each slot offset is an even number of samples, this is equivalent to one continuous phase-0 decimator from WAV sample zero.
Add a test that proves continuous-decimation and per-slot-seek implementations yield the same kept sample indices.
No FIR/resampler in this task.

## Per-slot dedupe

Current one-window dedupe is exact 75-bit payload identity.
In multi-slot mode, reset that dedupe set for every slot.
Do NOT dedupe across slots.
The same heartbeat or payload transmitted in consecutive slots must appear once in each slot.

## Output

Single-window invocation must preserve T060 stdout/stderr exactly for existing regressions.

For --all-slots, every decoded frame line must identify its aligned slot, for example:

    slot=0 slot_s=0 ...
    slot=1 slot_s=15 ...
    slot=2 slot_s=30 ...

Exact placement may be chosen for readability, but it must be stable and tested.
Candidate diagnostics on stderr must also identify slot when multi-slot mode is active.

Add one per-slot summary, for example:

    slot=2 blocks=93 candidates=... valid=... unique=...

and one final multi-slot summary:

    slots=8 trailing_input_samples=0

Do not label candidate time= as absolute UTC DT.
slot_s is exact elapsed time from aligned WAV PCM sample zero.

## JSC/Huffman/content behavior

All T056-T060 content decoding must work unchanged in every slot:

- heartbeat/CQ
- compound
- directed
- Huffman DATA
- JSC DATA_COMPRESSED

Open/validate JSC resource lazily once, then reuse it.
A JSC resource error in a later slot follows the existing T060 host error contract.
No message association/reassembly in T061.

## Synthetic multi-slot integration tests

Add deterministic host tests using fixed accepted payload vectors plus the existing PHY encoder only for waveform generation.

At minimum cover:

1. two full aligned slots with different signals, both decoded with correct slot tags;
2. repeated identical heartbeat/payload in two slots, appearing once in each slot;
3. signal only in slot 1: default invocation does not print it, --all-slots prints slot=1;
4. four-slot / one-minute capture with exact slot indices 0..3;
5. mix heartbeat, directed, Huffman DATA, and JSC DATA_COMPRESSED across slots;
6. JSC dictionary opened/reused across multiple compressed slots without per-slot engine allocation;
7. nonzero trailing partial data after final full slot: full slots decode, exact trailing_input_samples reported, partial final slot not decoded;
8. data in each slot's final 1440 skipped input samples cannot affect that slot or shift the next boundary;
9. malformed/truncated RIFF after otherwise valid aligned audio still rejects before DSP;
10. single-window no-option A_2_1 output remains byte-for-byte T060 compatible.

Keep synthetic test runtime reasonable; 2-4 actual audio slots are sufficient if lower-level arithmetic tests cover larger counts.

## One/two minute arithmetic

Add cheap non-audio tests for:

    60 s  = 720000 input samples = 4 slots
    120 s = 1440000 input samples = 8 slots

and exact slot starts 0, 180000, 360000, ...
No wall-clock API is needed.

## Optional real WebSDR use

No specific user WebSDR file is required as an automated fixture.
After review, the architect may test:

    ./build-linux/js8_decode --all-slots aligned-1min.wav

Expected output is all decoded frames from each complete aligned slot.
This optional experiment is not a gate.

## Documentation

Update docs/js8/application-protocol.md or the host-tool section to record:

- aligned multi-slot host mode;
- exact 180000-sample slot stride;
- 178560 samples consumed for DSP;
- 1440 samples skipped per slot;
- sample 0 alignment requirement;
- per-slot dedupe;
- no reassembly yet.

Do not imply that embedded JS8Chat requires WAV slot seeking.

## Architectural constraints

- host feature only
- no JS8 DSP algorithm change
- no monitor capacity change
- no platform/filesystem dependency added to js8_engine
- no heap added to js8_engine
- reuse one monitor workspace across slots
- no wall clock / UTC dependency
- no overlap/sliding search
- no message reassembly
- Normal mode only

## Non-goals

Do NOT implement automatic WAV alignment detection, overlapping windows, arbitrary start-time recovery, UTC parsing, live multi-slot UAC service, FIRST/LAST reassembly, directed/data association, conversation state, station activity tables, TX, scheduler/UI, or other JS8 modes.

## Acceptance criteria

- [x] --all-slots added without changing default first-window behavior
- [x] slot stride is exactly 180000 input samples
- [x] each slot feeds exactly 178560 input samples / 93 engine blocks
- [x] final 1440 input samples per full slot are skipped
- [x] phase-0 decimation remains equivalent across slot seeks
- [x] all complete 15-second slots are decoded
- [x] partial trailing slot is ignored/reported
- [x] payload dedupe resets per slot
- [x] repeated payloads in different slots are retained
- [x] multi-slot output has exact slot index / elapsed seconds
- [x] candidate diagnostics identify slot in multi-slot mode
- [x] one monitor workspace reused across slots
- [x] JSC resource reused across slots
- [x] Huffman/JSC/application decoding unchanged
- [x] real A_2_1 default output unchanged
- [x] full RIFF validation unchanged
- [x] no js8_engine platform dependency
- [x] no FT8 changes
- [x] Linux full CTest passes
- [x] portable CTest passes
- [x] external A_2_1 regression passes
- [x] boundary/no-heap checks pass
- [x] ASan/UBSan passes
- [x] ADV build green
- [x] git diff --check passes
- [x] no unrelated cleanup

No hardware/RF validation required.

## Branch workflow

Use:

    codex/T061-js8-multislot-wav

Codex:

1. read AGENTS.md, T059/T060, current js8_decode, and canonical JS8 docs;
2. preserve default one-window behavior exactly;
3. add explicit aligned --all-slots host mode using exact sample arithmetic;
4. reuse monitor/JSC state across slots;
5. reset payload dedupe per slot;
6. add slot tags and stable summaries only in multi-slot mode;
7. add synthetic aligned multi-slot regressions and arithmetic/phase tests;
8. do not add overlap, auto-alignment, or message reassembly;
9. run the full existing JS8 gates;
10. set Status to REVIEW;
11. fill implementation/test evidence;
12. push one reviewable commit and return SHA;
13. no PR and no Actions wait.

## Codex implementation notes

### Implementation summary

Added explicit host-only `--all-slots` decoding. The validated RIFF reader retains
its PCM offset/count and seeks to each exact aligned slot. One monitor workspace
and lazily opened JSC dictionary serve the entire invocation; monitor state and
payload dedupe reset per slot. Multi-slot output adds stable slot/elapsed tags,
per-slot diagnostics, and a final complete-slot/trailing-sample summary.
No deviations from task scope.

### Files changed

- `apps/js8chat/tools/js8_decode.c`: exact slot geometry, seeks, lifecycle, CLI and output.
- `tests/js8_multislot_host_test.c`: actual read/seek phase checks and instrumented host runner.
- `tests/js8_multislot_wav_test.py`: synthetic aligned audio and error regressions.
- `tests/js8_wav_reference.py`: exact T060 stdout/stderr and tagged real-WAV regression.
- `CMakeLists.txt`: register the two host tests.
- `docs/js8/application-protocol.md`: aligned host-mode contract.
- This task packet: implementation and gate evidence.

### Invariants preserved

Default valid one-window output remains byte-for-byte T060 compatible. Complete
RIFF validation still precedes DSP. No changes to `js8_engine`, FT8 production,
PHY algorithms, monitor capacity, resource format, or application decoding.
No platform access or allocation added to the engine. No overlap, alignment
search, wall clock, association, or reassembly.

### Multi-slot/sample arithmetic evidence

Compile-time assertions pin 178560 consumed input samples and 1440 skipped input
samples per 180000-sample slot. The host unit exercises actual seeks and all
93 reads per slot over eight slots, comparing every kept sample against a
continuous phase-0 sequence. Four/eight slot counts and exact byte positions pass.

Synthetic integration covers different/repeated payloads, signal only in slot 1,
four mixed heartbeat/directed/Huffman/JSC slots, ignored partial data, hostile odd
samples, changed skipped tails, and malformed trailing RIFF. Two compressed slots
produce two results with measured instrumentation:

    probe workspace_allocations=1 dictionary_opens=1 stream_resets=2

The external WAV blob remains `d986a4e5a9cc654dffbfadae73ec35cc9cea1d83`.
`cmp` against saved T060 stdout and stderr passes for the default invocation.
The exact output is also pinned by the reference test. Multi-slot real-WAV stdout:

```text
slot=0 slot_s=0 payload=111001011101001010000111001011100000101011000001100010000111111111111111010 type=2 frame="vTA7BWh1Y7++" tx_raw=2 class=data_compressed tx=LAST score=26 time=5/0 freq=57/0 hz=556.250 hard_errors=15 codec=jsc data="MSG ID 416"
```

Its per-slot and final summaries:

```text
slot=0 blocks=93 ignored_engine_samples=720 candidates=50 ldpc_fail=49 crc_fail=0 valid=1 unique=1
slots=1 trailing_input_samples=0
```

### Test evidence

All required gates passed locally:

```sh
cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --repeat until-pass:3 --output-on-failure

cmake -S tests/unit -B /tmp/T061-build-unit
cmake --build /tmp/T061-build-unit -j"$(nproc)"
ctest --test-dir /tmp/T061-build-unit --output-on-failure

cmake -S . -B /tmp/T061-build-ref -DJS8_A2_1_REFERENCE_WAV="$HOME/projects/js8chat/A_2_1.wav"
cmake --build /tmp/T061-build-ref -j"$(nproc)"
ctest --test-dir /tmp/T061-build-ref -R "js8.*reference|js8.*A2.*1" --output-on-failure

PYTHONDONTWRITEBYTECODE=1 python3 tests/app_dependency_boundary.py . js8chat
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_platform_boundary.py . js8chat

cmake -S . -B /tmp/T061-build-sanitize -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -g" -DJS8_A2_1_REFERENCE_WAV="$HOME/projects/js8chat/A_2_1.wav"
cmake --build /tmp/T061-build-sanitize -j"$(nproc)" --target js8_rx_unit js8_phy_unit js8_frame_unit js8_protocol_frame_unit js8_compound_unit js8_directed_unit js8_huffman_unit js8_jsc_unit js8_decode js8_multislot_host_unit
ctest --test-dir /tmp/T061-build-sanitize -R '^js8_(phy_unit|rx_unit|frame_unit|protocol_frame_unit|compound_unit|directed_unit|huffman_unit|jsc_unit|wav_unit|directed_wav_unit|huffman_wav_unit|jsc_wav_unit|multislot_host_unit|multislot_wav_unit|A2_1_reference)$' --output-on-failure

source ~/projects/esp-idf/export.sh
idf.py -C platform/adv build
git diff --check
```

- Linux: initial run 104/105; existing intermittent `linux_serial_unit` PTY
  assertion at line 67 (`n == 0`) failed. Full retry passed 105/105, each test on
  its first attempt. No serial code or test changes.
- Portable: 23/23 passed.
- External A_2_1 reference: 1/1 passed, checking both default and all-slots output.
- Boundary checks and existing no-heap checks: passed.
- ASan/UBSan: 15/15 passed, including JSC resource corruption, new multi-slot
  tests, and external WAV. Run outside the sandbox for LeakSanitizer support.
- ADV build passed; application image `0x151790` bytes, 78% partition free.
- Whitespace check passed.

### Manual validation still required

None required for this host-only task. Optional architect experiment with an
aligned one/two-minute WebSDR recording remains available after review. No RF or
hardware acceptance gate, PR, or GitHub Actions wait.

### Known limitations / risks

Sample zero must already be aligned. Only complete 15-second slots decode in
explicit multi-slot mode; trailing partial input is reported and ignored. The
existing phase-0 decimator and Normal decoder sensitivity are unchanged. Frames
remain independent; no cross-slot association or reassembly. The external WAV
is not committed.

### Commit

One reviewable implementation commit on `codex/T061-js8-multislot-wav`; the exact
SHA is returned in the Codex handoff (this packet is part of that commit).

## Supervisor review

## Architect test result

No required hardware/RF acceptance. Optional aligned WebSDR multi-slot run after review.

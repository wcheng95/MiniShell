# T061 — Aligned multi-slot JS8 WAV decoder

Status: READY

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

- [ ] --all-slots added without changing default first-window behavior
- [ ] slot stride is exactly 180000 input samples
- [ ] each slot feeds exactly 178560 input samples / 93 engine blocks
- [ ] final 1440 input samples per full slot are skipped
- [ ] phase-0 decimation remains equivalent across slot seeks
- [ ] all complete 15-second slots are decoded
- [ ] partial trailing slot is ignored/reported
- [ ] payload dedupe resets per slot
- [ ] repeated payloads in different slots are retained
- [ ] multi-slot output has exact slot index / elapsed seconds
- [ ] candidate diagnostics identify slot in multi-slot mode
- [ ] one monitor workspace reused across slots
- [ ] JSC resource reused across slots
- [ ] Huffman/JSC/application decoding unchanged
- [ ] real A_2_1 default output unchanged
- [ ] full RIFF validation unchanged
- [ ] no js8_engine platform dependency
- [ ] no FT8 changes
- [ ] Linux full CTest passes
- [ ] portable CTest passes
- [ ] external A_2_1 regression passes
- [ ] boundary/no-heap checks pass
- [ ] ASan/UBSan passes
- [ ] ADV build green
- [ ] git diff --check passes
- [ ] no unrelated cleanup

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

### Files changed

### Invariants preserved

### Multi-slot/sample arithmetic evidence

### Test evidence

### Manual validation still required

### Known limitations / risks

### Commit

## Supervisor review

## Architect test result

No required hardware/RF acceptance. Optional aligned WebSDR multi-slot run after review.

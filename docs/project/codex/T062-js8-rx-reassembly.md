# T062 — Directed free-text RX reassembly

Status: COMPLETE

## Architect intent

Turn the independently decoded DIRECTED/Huffman/JSC frames into practical keyboard-to-keyboard receive messages.

T061 now provides exact aligned multi-slot host captures. T062 adds a small pure application-layer reassembler that can associate a standard free-text directed header with following DATA/DATA_COMPRESSED fragments across consecutive Normal slots.

This is the first message-level RX milestone.

Keep compound-directed association, conversation history, UI, TX, ACK automation and buffered/query commands out of scope.

## Upstream wire behavior to preserve

Frozen reference:

    JS8Call-improved/JS8Call-improved v3.0.3

Relevant source:

    JS8_Main/Varicode.cpp buildMessageFrames()
    JS8_Mainwindow/processDecodeEvent.cpp
    JS8_Mainwindow/processBufferedActivity.cpp
    JS8_UI/mainwindow.cpp hasExistingMessageBuffer()

For one logical transmitted line, upstream marks:

    lineFrames.first() |= JS8CallFirst
    lineFrames.last()  |= JS8CallLast

For ordinary standard directed free text this gives:

    slot N:
        DIRECTED
        from=AG6AQ
        to=K1ABC
        cmd=free text (31)
        FIRST

    slot N+1:
        DATA or DATA_COMPRESSED
        text fragment

    ...

    slot N+k:
        DATA or DATA_COMPRESSED
        text fragment
        LAST

A one-frame directed command may carry FIRST|LAST. ACK/73 remain standalone commands and are not text reassembly streams.

T062 handles only standard directed free-text command 31 plus its continuation DATA frames.

## Stream key

Upstream desktop buffering is frequency-offset based and allows Normal-mode drift within:

    JS8 Normal rxThreshold = 10 Hz

Use the same tolerance.

Do not require an exact candidate bin/sub-bin match across slots.

Keep the pure reassembler independent of floating point by passing a signed integer frequency in milli-Hz:

    frequency_millihz

Host conversion from current candidate geometry is exact:

    hz = min_bin*6.25 + freq_offset*6.25 + freq_sub*3.125

so:

    frequency_millihz =
        min_bin*6250 +
        freq_offset*6250 +
        freq_sub*3125

Match a continuation to an open stream when:

    abs(new_frequency_millihz - stream_frequency_millihz) <= 10000

If multiple streams fall in range, choose the closest absolute difference.
On an exact tie, choose the lowest stable context index.

After a match, update the stream's stored frequency to the newest value, analogous to upstream buffer drift.

## Pure reassembly layer

Add a platform-independent module under:

    apps/js8chat/src/js8_engine/

Suggested:

    js8_reassembly.[ch]

The module must not perform PHY decoding, Huffman/JSC decoding, file access, UI, logging, or clock access.

The caller supplies already-decoded semantic events.

Suggested normalized event:

    typedef enum {
        JS8_RX_FRAGMENT_DIRECTED,
        JS8_RX_FRAGMENT_DATA
    } Js8RxFragmentKind;

    typedef struct {
        Js8RxFragmentKind kind;
        uint32_t slot_index;
        int32_t frequency_millihz;
        uint8_t tx_flags;

        // DIRECTED:
        char from[JS8_CALLSIGN28_SIZE];
        char to[JS8_CALLSIGN28_SIZE];
        uint8_t command_code;

        // DATA:
        const char *text;
        uint16_t text_len;
    } Js8RxFragment;

Exact API may differ, but the reassembler should depend only on normalized semantic fields.

Do not make the reassembler depend directly on JSC dictionary or monitor types.

## Fixed contexts

Use a small fixed context table suitable for ADV.

Freeze T062 baseline at:

    4 concurrent receive streams

Each stream should hold at least:

    active
    from
    to
    command_code
    latest frequency_millihz
    first_slot
    last_slot
    accumulated text
    text_len
    gap/error state

Use no heap.

For message text, freeze a practical baseline:

    1024 bytes per context including NUL

This costs about 4 KiB for four active streams and is reasonable for ADV.

Return an explicit overflow result and clear/drop the affected stream rather than truncating silently.

Do not persist conversation history here; this is temporary RF reassembly only.

## Start behavior

A DIRECTED fragment opens a text stream only when all are true:

    application class was DIRECTED
    command_code == 31 (free text)
    FIRST flag is set
    from != "<....>"
    to   != "<....>"

On FIRST:

- find an existing stream within +/-10 Hz;
- if one exists, replace/clear it;
- otherwise use a free context;
- if no free context exists, evict the stalest context deterministically;
- store from/to/frequency/slot;
- clear accumulated text.

If the same directed header also has LAST:

- immediately emit a complete empty free-text message;
- clear the context.

A directed free-text header without FIRST does not open a new stream.

ACK, 73, SNR and all other commands do not open a text stream in T062.

Compound placeholders remain deferred.

## DATA behavior

A DATA or DATA_COMPRESSED fragment is supplied to the reassembler only after its codec has successfully produced text.

For each DATA fragment:

1. find the closest active stream within +/-10 Hz;
2. if none exists, report ORPHAN and leave state unchanged;
3. check slot progression;
4. append exact decoded bytes;
5. update latest frequency and last_slot;
6. if LAST is set, emit the completed message and clear the context.

Huffman and JSC fragments are identical at this layer; concatenate bytes exactly in receive order.

Do not insert or remove spaces between fragments.

Upstream codecs already determine fragment text.

## Slot progression / lost-frame detection

Normal directed TX sends the logical frames in consecutive 15-second slots.

Use slot_index supplied by the caller.

For a matched continuation:

    expected next slot = context.last_slot + 1

If slot_index == expected:
    append normally

If slot_index <= context.last_slot:
    classify as stale/duplicate and do not append

If slot_index > expected:
    mark the stream incomplete due to a gap

For a gapped stream:

- you may continue consuming later fragments to find LAST;
- do not emit it as a valid complete chat message;
- when LAST arrives, return an INCOMPLETE/GAP result and clear the stream.

This prevents silently presenting a message with a missing middle frame as complete.

No sequence number exists on air; slot-gap detection is the only missing-frame evidence available in this bounded task.

## Timeout / stale cleanup

Do not synthesize LAST after 60 seconds.

For safety and simplicity, expire an unfinished stream once a new event arrives more than 90 seconds after its last fragment.

With 15-second slot indices:

    expire if new_slot - last_slot > 6

This follows upstream's 90-second stale-buffer removal boundary without its 60-second forced-close behavior.

Expiry returns a deterministic STALE/DROPPED status when useful, but must not emit a complete message.

## Completion output

Suggested completed message:

    typedef struct {
        char from[JS8_CALLSIGN28_SIZE];
        char to[JS8_CALLSIGN28_SIZE];
        int32_t frequency_millihz;
        uint32_t first_slot;
        uint32_t last_slot;
        char text[1024];
        uint16_t text_len;
    } Js8RxMessage;

The feed API should distinguish:

    accepted/no completion
    complete message
    orphan DATA
    duplicate/stale fragment
    gap/incomplete completion
    overflow/drop
    invalid argument

Exact enum naming is implementation choice.

All error paths must be deterministic and bounds safe.

## Multiple simultaneous streams

Test at least four concurrent directed free-text streams on separated audio offsets.

DATA fragments must attach to the correct stream by nearest frequency within +/-10 Hz.

Interleave their slots in test event order.

Example:

    600 Hz: W6AAA -> AG6AQ
    750 Hz: K6BBB -> AG6AQ
    900 Hz: N7CCC -> AG6AQ
    1050 Hz: W7DDD -> AG6AQ

No conversation/UI state is implied; this only proves temporary RF stream separation.

## Frequency drift tests

For one stream, exercise:

    header 700.000 Hz
    DATA   706.250 Hz
    DATA   696.875 Hz

Both are within +/-10 Hz from the latest matched position and must remain one stream.

Also prove a fragment >10 Hz away is orphaned.

Test ambiguous nearby streams and deterministic closest-match/tie behavior.

## Host integration

Extend only explicit multi-slot host mode with an optional message layer:

    ./build-linux/js8_decode --all-slots --messages aligned.wav

or an equivalent stable CLI ordering.

Preserve:

    ./build-linux/js8_decode file.wav
    ./build-linux/js8_decode --all-slots file.wav

byte-for-byte for existing T060/T061 tests.

With --messages:

- continue printing all normal raw frame lines and per-slot diagnostics;
- additionally feed decoded standard free-text DIRECTED and decoded DATA/JSC fragments to the reassembler;
- print a stable completed-message line when LAST closes a valid stream.

Suggested:

    message from=AG6AQ to=K1ABC first_slot=0 last_slot=2 hz=700.000 text="HELLO WORLD"

Escape text using the same stable host escaping rules already used for JSC/Huffman diagnostics.

For gap/overflow/drop events, print stable stderr diagnostics but do not fabricate a completed message.

Do not require the message destination to equal the local station callsign in the host tool; WebSDR monitoring should reconstruct any standard directed free-text stream it hears.

## Candidate frequency conversion

Do not round the existing floating-point diagnostic back into a stream key.

Compute exact milli-Hz directly from candidate integer fields:

    frequency_millihz =
        req.min_bin * 6250
        + candidate.freq_offset * 6250
        + candidate.freq_sub * (6250 / cfg.freq_osr)

For current freq_osr=2 the sub-step is 3125 mHz.

Keep the existing printed hz= diagnostic unchanged.

## Synthetic multi-slot message tests

Build host fixtures from already accepted fixed application payloads; test-only code may change the final three tx flags without changing application bits.

At minimum:

1. two-slot message:
   - DIRECTED free-text + FIRST
   - Huffman DATA "HELLO WORLD" + LAST
   - completed text exactly HELLO WORLD

2. three-slot mixed-codec message:
   - DIRECTED + FIRST
   - Huffman DATA middle
   - JSC DATA_COMPRESSED + LAST
   - exact byte concatenation

3. one header with FIRST|LAST:
   - emits empty free-text message

4. orphan DATA + LAST:
   - no completed message

5. missing middle slot:
   - header slot 0
   - DATA LAST slot 2
   - gap/incomplete, no valid message

6. duplicate DATA event in same slot:
   - appended once only

7. frequency drift within +/-10 Hz:
   - reassembles

8. drift >10 Hz:
   - orphan/no completion

9. four concurrent streams:
   - independently complete with correct from/to/text

10. new FIRST on same frequency:
    - replaces stale/open stream

11. 90-second stale expiry:
    - stale context removed, no fabricated completion

12. 1024-byte overflow:
    - explicit drop, no truncation

13. standard ACK and 73:
    - remain standalone raw directed frames
    - never create text contexts

14. placeholder <....> directed header:
    - no text context in T062

15. repeated raw payload across separate slots:
    - T061 per-slot output remains unchanged
    - reassembler duplicate policy is based on stream/slot event semantics, not global payload suppression

## Pure unit tests

Unit tests must not require WAV, monitor, filesystem or JSC resource.

Feed normalized semantic fragments directly.

Cover all status transitions, all four contexts, frequency matching, gap handling, overflow, timeout, replacement and canary preservation.

Portable CTest must include the reassembler.

## Documentation

Update docs/js8/application-protocol.md:

- FIRST is on the first logical frame, normally the DIRECTED header;
- LAST is on the final logical frame, normally the last DATA fragment;
- standard free-text stream association is frequency based with +/-10 Hz Normal tolerance;
- T062 uses four fixed contexts / 1024-byte temporary message buffers;
- gaps are not presented as complete messages;
- 90-second stale streams expire;
- conversation history/UI remain separate.

## Architectural constraints

- pure C reassembler
- no heap
- no mutable global singleton
- no platform/filesystem
- no monitor/DSP dependency in reassembly module
- no JSC resource dependency in reassembly module
- fixed four contexts
- fixed 1024-byte message buffers
- Normal only
- exact decoded text bytes preserved

## Non-goals

Do NOT implement:

- compound-call association for <....>
- COMPOUND_DIRECTED command semantics
- buffered MSG/QUERY/CMD command processing
- checksums for buffered commands
- conversation history
- unread flags/UI
- local-station filtering
- automatic ACK/HB response
- TX frame construction
- JSC TX
- live MiniShell/UAC integration
- other JS8 modes

## Acceptance criteria

- [x] pure fixed-state reassembler added
- [x] four concurrent contexts supported
- [x] standard free-text DIRECTED+FIRST opens stream
- [x] DATA/Huffman and DATA_COMPRESSED/JSC append identically
- [x] LAST completes and clears stream
- [x] FIRST|LAST header completes empty message
- [x] +/-10 Hz frequency matching implemented
- [x] nearest/tie behavior deterministic
- [x] frequency drift updates stream key
- [x] per-slot progression checked
- [x] duplicate/stale fragments not appended twice
- [x] gaps never produce a valid complete message
- [x] stale streams expire after >90 seconds
- [x] overflow drops instead of truncating
- [x] orphan DATA is explicit
- [x] ACK/73 do not open text streams
- [x] placeholders remain deferred
- [x] pure unit tests cover state machine
- [x] --messages host mode added without changing default/T061 output
- [x] completed message lines are stable and tested
- [x] mixed Huffman/JSC multi-slot message reconstructs exact bytes
- [x] four concurrent synthetic streams reassemble correctly
- [x] T061 multi-slot behavior unchanged without --messages
- [x] T060 A_2_1 default output unchanged
- [x] T056-T060 content semantics unchanged
- [x] js8_engine remains no-heap/platform-free
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

    codex/T062-js8-rx-reassembly

Codex:

1. read AGENTS.md, canonical JS8 docs, T058-T061, and pinned upstream buildMessageFrames/buffer behavior;
2. implement normalized pure reassembly state first;
3. keep codec/DSP/resource concerns outside the module;
4. add exhaustive pure state-machine tests;
5. add optional --messages integration on top of --all-slots only;
6. use exact milli-Hz candidate frequency conversion and +/-10 Hz matching;
7. preserve all existing host output when --messages is absent;
8. do not implement compound association, buffered commands, conversations or TX;
9. run the full JS8 gate set;
10. set Status to REVIEW;
11. fill implementation/test evidence;
12. push one reviewable commit and return SHA;
13. no PR and no Actions wait.

## Codex implementation notes

### Implementation summary

Implemented the pure fixed-state reassembler first, then connected it to the
optional `--all-slots --messages` host mode. A normalized event API owns no codec,
DSP, filesystem, clock, heap, or singleton state. Four contexts preserve exact
text bytes, enforce consecutive slot progression, and only emit on valid LAST.
Status and cleanup masks distinguish incomplete/gap, orphan, duplicate, overflow,
expiry, replacement, and eviction. No scope deviations.

Reviewed pinned v3.0.3 `buildMessageFrames()` FIRST/LAST assignment,
`processDecodeEvent.cpp` buffer replacement/append behavior,
`processBufferedActivity.cpp` LAST/90-second stale removal, and
`hasExistingMessageBuffer()` frequency drift matching. The task deliberately
omits desktop forced LAST at 60 seconds and whitespace trimming, while freezing
nearest-frequency/context-index ties and strict gap detection. Source SHA-256:

```text
JS8_Main/Varicode.cpp
2b4a877e7dae1a3fdd9141f4dd8af422185977d2e9363ad23ef6bc1918eaf53a
JS8_Mainwindow/processDecodeEvent.cpp
168a7b20765d9a8f46fda2c480f56e4e9cdf3e1e9624bc84edce0d4748b0e270
JS8_Mainwindow/processBufferedActivity.cpp
283aabb47b7f03c82abbc6a1a2740d6c4ca8a9d095a19172b7ffd10c4a754d60
JS8_UI/mainwindow.cpp
a6cef3f55add36791386eafd75736ce2d84bf578625fb591ec66b5cfe60e04a8
```

### Files changed

- `apps/js8chat/src/js8_engine/js8_reassembly.[ch]`: normalized events,
  caller-owned fixed state, completion output and deterministic result/drop API.
- `apps/js8chat/tools/js8_decode.c`: optional message mode, exact integer
  candidate frequency conversion, semantic feed, escaped message output.
- `tests/js8_reassembly_test.c`: pure state-machine and canary tests.
- `tests/js8_reassembly_wav_test.py`: synthetic end-to-end message regressions.
- `tests/js8_multislot_host_test.c`: direct bin/sub-bin conversion checks.
- `tests/js8_tests.cmake`, `CMakeLists.txt`: portable/Linux unit and host gates.
- `docs/js8/application-protocol.md`: implemented temporary RX state contract.
- This task packet: implementation and gate evidence.

### Invariants preserved

Only standard DIRECTED command 31 + FIRST without placeholders opens a stream.
FIRST|LAST completes empty text. Both codecs append bytes identically without
space insertion/removal; transmission flags stay separate from application class.
Closest frequency within inclusive 10000 mHz wins; matching updates drift and
exact ties use the lowest stable index. Stalest eviction compares last_slot,
with the same index tie rule. Four buffers each reserve 1024 bytes including NUL.
Overflow clears instead of truncating. Duplicate/stale DATA does not append;
gaps persist until LAST/drop and never become valid messages. Expiry is strictly
more than six slots, with no synthetic LAST or end-of-file completion.

Default and ordinary all-slots outputs remain unchanged. Existing PHY, CRC,
LDPC, codecs, resource callback, RIFF validation and T061 geometry/dedupe are
unchanged. No FT8 changes, compound association, buffered commands, persistent
conversations, platform integration, UI, auto-replies or TX were added.

### Reassembly/state-machine evidence

The pure portable test covers every status, all four concurrent streams,
nearest/tied matches, exact +/-10000 mHz boundaries, drift, FIRST replacement,
deterministic full-table eviction, duplicates/out-of-order events, gaps, expiry
at >90 seconds, ignored commands 0..30, missing FIRST, both placeholder fields,
empty LAST, 1023-byte capacity and overflow, exact embedded-NUL bytes, invalid
arguments without state/output mutation, and surrounding state/output canaries.
Extreme signed frequencies and non-wrapping UINT32_MAX slots are tested.

Measured on the Linux build:

```text
Js8RxReassembly = 4272 bytes
Js8RxContext    = 1068 bytes (including 1024-byte text storage)
Js8RxMessage    = 1064 bytes
js8_reassembly.c.o: text=1842 data=0 bss=0 bytes
```

The compiled no-heap gate covers the new source through `js8_phy_core`.

### Multi-slot host evidence

Fixtures use fixed accepted T058/T059/T060 application bits; test-only code alters
only final TX flags and uses the existing PHY encoder for waveform generation.
Representative exact completed lines:

```text
message from=AG6AQ to=K1ABC first_slot=0 last_slot=1 hz=1000.000 text="HELLO WORLD"
message from=AG6AQ to=K1ABC first_slot=0 last_slot=2 hz=1000.000 text="HELLOMSG ID 416"
message from=AG6AQ to=K1ABC first_slot=0 last_slot=2 hz=696.875 text="HELLOMSG ID 416"
```

Four simultaneous audio streams at 600/750/900/1050 Hz independently complete
with the four distinct accepted header address pairs and expected texts. Host
fixtures also cover orphan LAST, missing middle slot, out-of-range drift,
replacement, empty FIRST|LAST, expiry, ACK/73/placeholders, escaped quotes/newline,
and repeated identical payloads across slots. A 32-slot overflow fixture retains
all 32 raw frame lines and reports `slot=31 reassembly=overflow`, with no complete
message. Same-slot duplicate semantics are exercised directly in the pure unit
because T061 raw dedupe intentionally filters those before host semantic feed.

Every message fixture compares its raw stdout and original stderr against the
same WAV without `--messages`; only added message/reassembly lines differ.
`cmp` also matches both stdout and stderr against saved T060 default and T061
all-slots A_2_1 evidence. The external WAV hash is still
`d986a4e5a9cc654dffbfadae73ec35cc9cea1d83`; no WAV is committed.

### Test evidence

Commands (all local):

```sh
cmake -S . -B build-linux
cmake --build build-linux -j8
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure

cmake -S tests/unit -B /tmp/T062-build-unit
cmake --build /tmp/T062-build-unit -j8
ctest --test-dir /tmp/T062-build-unit --output-on-failure

cmake -S . -B /tmp/T062-build-ref -DJS8_A2_1_REFERENCE_WAV="$HOME/projects/js8chat/A_2_1.wav"
cmake --build /tmp/T062-build-ref -j8 --target js8_decode
ctest --test-dir /tmp/T062-build-ref -R "js8.*reference|js8.*A2.*1" --output-on-failure

PYTHONDONTWRITEBYTECODE=1 python3 tests/app_dependency_boundary.py . js8chat
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_platform_boundary.py . js8chat

cmake -S . -B /tmp/T062-build-sanitize -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -g" -DJS8_A2_1_REFERENCE_WAV="$HOME/projects/js8chat/A_2_1.wav"
cmake --build /tmp/T062-build-sanitize -j8 --target js8_rx_unit js8_phy_unit js8_frame_unit js8_protocol_frame_unit js8_compound_unit js8_directed_unit js8_huffman_unit js8_jsc_unit js8_reassembly_unit js8_decode js8_multislot_host_unit
ctest --test-dir /tmp/T062-build-sanitize -R '^js8_(phy_unit|rx_unit|frame_unit|protocol_frame_unit|compound_unit|directed_unit|huffman_unit|jsc_unit|reassembly_unit|wav_unit|directed_wav_unit|huffman_wav_unit|jsc_wav_unit|multislot_host_unit|multislot_wav_unit|reassembly_wav_unit|A2_1_reference)$' --output-on-failure

source ~/projects/esp-idf/export.sh
idf.py -C platform/adv build
git diff --check
```

- Linux full CTest: 107/107 passed on the first full run.
- Portable CTest: 24/24 passed, including the pure reassembler.
- External A_2_1 regression: 1/1 passed, exact default and all-slots output.
- Dependency/platform boundary and compiled no-heap checks: passed.
- ASan/UBSan: 17/17 passed, including JSC corruption and message WAV tests;
  executed outside the sandbox for LeakSanitizer.
- ADV build: passed, image `0x151790` bytes, 78% app partition free. This task
  does not add live ADV integration.
- `git diff --check`: passed.

The initial focused host test found a missing-filename CLI case
(`--all-slots --messages`) returning file-open error instead of usage. Fixed the
argument check; the focused suite and subsequent full Linux run pass unchanged
assertions. No tests were weakened.

### Manual validation still required

None required. Optional aligned WebSDR `--all-slots --messages` testing remains
for the architect after review. No hardware/RF, PR, or GitHub Actions wait gate.

### Known limitations / risks

The host relies on T061's exact sample-zero alignment and complete slots.
Frequency proximity and slot continuity are the available association evidence;
there is no on-air sequence number or collision recovery in this bounded layer.
T061 exact-payload dedupe within a slot is preserved, including its treatment of
identical simultaneous raw payloads. Compound placeholders and non-free-text
commands stay deferred. Unfinished messages at end of capture are not emitted.

### Commit

The reviewed implementation commit is:

`e244b7bd6a0a8ffe2adf2bfddaad5d8392934f48`

No PR or GitHub Actions wait.

## Supervisor review

Reviewed commit `e244b7bd6a0a8ffe2adf2bfddaad5d8392934f48` against T062 and pinned JS8Call-improved v3.0.3 FIRST/LAST and receive-buffer behavior.

Result: **PASS**.

Review findings:

- One bounded implementation commit, one commit ahead of the T062 baseline.
- Pure reassembly state is fixed-size, caller-owned, no-heap, platform-free, and codec/DSP independent.
- Standard DIRECTED command 31 + FIRST opens a stream; ACK/73/other commands and `<....>` placeholders remain excluded.
- FIRST|LAST on a standard free-text header emits an empty complete message and clears state.
- DATA from either Huffman or JSC appends identical decoded bytes; no whitespace is inserted or removed.
- Stream matching uses inclusive +/-10 Hz in exact integer milli-Hz, chooses nearest context, and resolves exact ties to the lowest context index.
- Matching updates the stored frequency, allowing bounded drift across slots.
- Four concurrent contexts and deterministic oldest-context eviction are implemented exactly as specified.
- DATA at/before last_slot is treated as duplicate/stale and is not appended.
- Slot gaps permanently mark the stream incomplete; LAST on a gapped stream clears it without emitting a valid message.
- Unfinished streams expire only after more than six slots (>90 seconds), with no synthetic LAST.
- 1024-byte context buffers preserve 1023 data bytes plus NUL; overflow drops the stream rather than truncating.
- Invalid arguments preserve state/message/drop outputs; canary tests cover surrounding memory.
- Host candidate frequency is derived directly from integer bin/sub-bin geometry rather than rounded printed Hz.
- Optional `--all-slots --messages` preserves ordinary raw frame lines and diagnostics; removing message/reassembly additions reproduces T061 output exactly.
- Synthetic host tests cover Huffman/JSC mixed messages, four simultaneous streams, frequency drift, gaps, replacement, stale expiry, overflow, orphan DATA, ACK/73 exclusion, placeholder exclusion, and host escaping.
- T061 multi-slot behavior, T060 JSC/Huffman semantics, T054/T055 DSP/frontend, and FT8 remain unchanged.
- Reported gates are consistent with the diff: Linux 107/107, portable 24/24, sanitizer 17/17, real-WAV regression, boundaries/no-heap, ADV build, and diff check all pass.

Main was fast-forwarded to the reviewed implementation commit.

## Architect test result

No required hardware/RF acceptance. Optional aligned WebSDR --messages experiment remains available; task complete.

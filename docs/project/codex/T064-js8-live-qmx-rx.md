# T064 — Live Linux QMX JS8 RX monitor

Status: TESTING

## Architect intent

Connect the accepted JS8 RX stack to real live QMX audio on Linux and feed the exact T063 normalized activity/logger path.

This is a receive-only monitoring slice. TX remains deferred.

The app should be useful for leaving pc-1 on a JS8 band and recording real on-air activity for many slots.

## Product goal

On pc-1, run a MiniShell JS8 receive monitor against the QMX:

    js8chat --rx alsa:<QMX-device> \
             --dial-hz 14078000 \
             [--cat serial:<QMX-CDC>] \
             [--log /flash/js8chat/activity.jsonl]

and continuously:

    QMX UAC audio
      -> MiniShell Audio service
      -> 12 kHz S16 stereo
      -> JS8 6 kHz frontend
      -> UTC-aligned 15-second slots
      -> JS8 multi-signal RX
      -> T062 reassembly
      -> T063 activity events
      -> append-only js8-activity-v1 log

## Accepted baseline

- T052-T060: JS8 PHY/protocol/content RX COMPLETE.
- T061: aligned multi-slot WAV decode COMPLETE.
- T062: free-text RX reassembly COMPLETE.
- T063: normalized activity + JSONL schema/logger COMPLETE.
- MiniFT8 Linux Audio/Serial/QMX ownership is already proven on real QMX.

Read:

    AGENTS.md
    docs/js8/architecture.md
    docs/js8/application-protocol.md
    docs/js8/activity-log.md
    docs/project/codex/T063-js8-activity-log.md
    apps/ft8/src/rx_audio_adapter/*
    apps/ft8/src/rx_frontend/*
    apps/ft8/src/app_controller/app_controller.c
    apps/ft8/src/radio_control/radio_qmx.c
    platform/linux/linux_audio_wav.c

Do not create a second ALSA or tty stack inside JS8Chat.

## First MiniShell JS8Chat app slice

Create the first receive-only MiniShell application target:

    js8chat

under apps/js8chat/main plus JS8-owned live-RX modules.

The app must use only public MiniShell services:

    Audio
    Time/Location
    Filesystem
    Serial (optional CAT)
    Console/System
    Input only if needed for clean quit
    Memory

No POSIX/ALSA/termios/pthread/fopen/file-descriptor access is allowed under apps/js8chat production app code.

Linux provider details remain in platform/linux.

## CLI

Required live source:

    --rx <endpoint>

Linux QMX example:

    --rx alsa:hw:CARD=QMX,DEV=0

Do not hard-code a specific ALSA device string.

Optional metadata/control:

    --dial-hz <integer>
    --cat <serial endpoint>
    --log <MiniShell filesystem path>
    --slots <N>

Rules:

- --cat requires --dial-hz;
- --log is optional;
- --slots N stops cleanly after N fully processed JS8 slots and is primarily for deterministic tests/manual acceptance;
- omitted --slots means continuous monitoring until operator quit;
- q or equivalent MiniShell input should stop cleanly when the input service is available;
- malformed/duplicate/missing option values are rejected deterministically.

Do not add band tables in T064; explicit dial frequency avoids another policy table.

## MiniShell Audio ownership

Use the public MiniShell Audio RX service.

Open exactly:

    12000 Hz
    S16
    2 channels

On Linux, the accepted provider already maps an `alsa:` endpoint from native 48 kHz S24 stereo to this contract.

JS8Chat must not know that ALSA native format/rate.

Add a small JS8-owned audio adapter if useful, but it owns only application stream lifecycle and MiniShell handles.

## JS8 frontend

Add a JS8-owned pure frontend with the already-proven ordinary-audio behavior:

    normalize S16 L/R
    average L/R
    phase-0 2:1 decimation
    retain one-bit decimation phase across arbitrary transport chunks

Output:

    6000 Hz mono float

On a real Audio discontinuity:

- reset frontend decimation phase;
- invalidate UTC/sample alignment;
- reset active monitor capture;
- clear T062 reassembly contexts;
- reacquire timing on fresh audio.

Do not hide discontinuities by padding samples.

## Live UTC/sample timing — match MiniFT8 exactly

Live input begins at an arbitrary point in a 15-second JS8 slot.

**Do not establish one UTC anchor and then free-run indefinitely by sample count.**
That would accumulate error across slots if samples are lost or the audio clock differs
slightly from UTC.

Mirror the accepted MiniFT8 live timing model:

1. after **every successful live Audio read** and 12 kHz -> 6 kHz frontend conversion,
   query MiniShell UTC;
2. convert that current UTC to:

       slot_id = floor(unix_seconds / 15)
       sample_offset = seconds_into_slot*6000 + fractional_ns*6000/1e9

3. backdate that reference by the number of 6 kHz samples just produced so the
   resulting (slot_id, sample_offset) describes the **first sample of this chunk**;
4. convert that pair to an absolute 6 kHz sample position for slot scheduling;
5. use the chunk's fresh absolute timed position to decide whether/where the next
   exact JS8 slot boundary falls inside the chunk;
6. sample counting is used only to walk within the current timed chunk/capture,
   never as the long-term clock across later slots;
7. repeat this UTC-reference/backdate operation for every subsequent live chunk;
8. on Audio discontinuity, additionally reset frontend/timing/monitor/reassembly
   and resume only from a fresh timed chunk.

This is the same ownership model used by MiniFT8's live path:
`utc_to_slot_reference()` + `backdate_slot_reference()` are applied on each live
frontend output chunk before the timed samples are given to the live slot scheduler.

Do not use wall-clock sleeps to count samples.

Do not infer long-term slot time solely from the number of samples seen since startup.

Within one active 14.88-second capture, normal sample counting is still correct; the
fresh UTC references prevent **cross-slot cumulative drift**.

## Live slot geometry

At 6 kHz:

    full slot = 90000 samples
    decode window = 93 * 960 = 89280 samples
    slot tail = 720 samples

For every slot, its boundary is determined from the **fresh UTC-derived absolute
sample positions of incoming chunks**, not by adding 90000 forever to an old anchor.

When the timed chunk stream reaches:

    absolute slot boundary = slot_id * 90000

then:

- reset/begin the JS8 monitor at that exact boundary;
- feed exactly the next 89280 captured samples as 93 engine blocks;
- ignore samples belonging to the final 720-sample tail of that UTC slot;
- locate the following slot boundary again from the newly UTC-referenced chunks.

Thus 90000 is the exact slot geometry, but **not** a free-running long-term clock.

This is the live equivalent of T061:

    180000 input samples @12 kHz
    -> 178560 used
    -> 1440 skipped.

Do not overlap slots or perform a sliding search in T064.

## Decode/capture responsiveness

Live Audio service must continue being serviced while previous-slot decode work runs.

Do not solve this by buffering multiple seconds of raw audio.

Preferred architecture is waterfall/decode-workspace handoff, consistent with the project's existing rule that decoded work buffers waterfall rather than slot audio.

Implementation may use the existing accepted MiniFT8 platform-worker pattern or another bounded MiniShell-owned worker boundary, but:

- app code must not import pthread/FreeRTOS APIs directly;
- raw audio buffering beyond the small transport/frontend block is not allowed;
- no unbounded queue;
- at most a small fixed number of pending decode windows;
- if decode falls behind, drop/report a decode window explicitly rather than silently losing audio continuity.

For Linux manual acceptance, monitor at least 20 consecutive QMX slots without an Audio discontinuity caused by decode work.

Record decode/capture timing diagnostics sufficient to prove this.

## Multi-signal decode

Use the existing T054+ JS8 monitor/decoder candidate capacity and exact per-slot payload dedupe.

Do not reduce candidate capacity for live mode.

Each valid unique payload becomes the same semantic path already proven in js8_decode.

## Reassembly

Reuse T062 semantics unchanged:

- four contexts;
- +/-10 Hz nearest-frequency matching;
- FIRST/LAST;
- gap/expiry/overflow rules;
- Huffman and JSC fragments identical at this layer.

On Audio discontinuity, clear all temporary reassembly contexts because RF stream continuity is no longer trustworthy.

Do not synthesize MESSAGE events across a discontinuity.

## Activity events

Emit the exact T063 Js8Activity model for every valid unique frame and completed MESSAGE.

Do not invent another live-specific event schema.

Common live event fields:

    slot_index
    exact audio_millihz
    tx_flags
    score
    hard_errors

Use the real UTC slot identity for live events.

## Live JSONL logging

Reuse `js8-activity-v1` exactly.

The T063 host logger currently uses FILE. Refactor serialization only as much as necessary so both:

    js8_decode host FILE append
and
    js8chat MiniShell Filesystem append

produce byte-equivalent JSON objects for equivalent events/metadata.

Preferred boundary:

    semantic event
      -> platform-neutral JSON formatter / write callback
      -> host FILE sink OR MiniShell FS sink

Do not duplicate JSON escaping/schema logic in two implementations.

MiniShell live logger:

- append only;
- sync after each completed slot;
- close cleanly;
- explicit write/sync errors;
- no log rotation;
- no ADIF.

When --log is omitted, monitoring still works.

## Live UTC in log

Unlike T063 WAV metadata, live mode uses MiniShell UTC directly.

For each event, log the UTC corresponding to that event's slot boundary:

    slot_utc_seconds = slot_id * 15

Do not timestamp events using decode-completion wall clock.

This keeps every event in one RF slot on the same canonical UTC boundary.

## Dial/RF frequency

If --dial-hz is supplied:

    rf_millihz = dial_hz*1000 + audio_millihz

using exact integer arithmetic.

If absent, omit RF frequency exactly as T063 does.

If CAT is enabled, --dial-hz is required and is the same value used for QMX synchronization and activity metadata.

## Receive-safe QMX CAT

Optional --cat uses the public MiniShell Serial service.

Create a JS8-owned receive-control module; do not link JS8Chat against FT8 radio_control.

Exact startup commands:

    MD6;
    FR0;
    FT0;
    FA%011u;

No other CAT command is allowed in T064.

Explicitly forbidden in production JS8Chat T064:

    TX;
    RX;
    TA...
    TM...

No tone, tune, TX lifecycle, or transmit recovery.

CAT failure must fail startup cleanly and close the Serial handle.

Audio and CAT are separate resources just as in MiniFT8.

## Console behavior

This is a monitor, not the final 20x7 UI.

Keep console output compact.

At minimum print completed semantic observations useful for manual validation:

    slot UTC / audio Hz
    HB/CQ callsign/grid
    directed header
    completed MESSAGE

Raw candidate spam should stay diagnostic/debug output, not normal console output.

Do not implement station browser/conversation UI yet.

## Tests — pure/live components

Add deterministic hardware-free tests for:

1. Audio adapter lifecycle and MiniShell service validation;
2. arbitrary transport chunk sizes preserving 12k->6k decimation phase;
3. UTC -> slot/sample conversion including negative epoch edge cases if supported;
4. backdating every produced live chunk to its first sample;
5. repeated per-chunk UTC references do not accumulate cross-slot sample-count drift;
6. startup in the middle of a slot: use timed chunk positions to find the next boundary, then capture 89280;
7. deliberately perturb successive chunk UTC/sample positions to prove the next slot re-aligns to UTC rather than an old +90000 counter;
8. exact 90000-sample slot geometry;
9. exact 720-sample live tail skip;
10. chunks crossing slot boundaries;
11. discontinuity resets frontend/timing/monitor/reassembly;
12. no MESSAGE completed across a discontinuity;
13. multiple consecutive slots and repeated HB;
14. four simultaneous streams and reassembled MESSAGE;
15. T063 live JSON identical to equivalent WAV event JSON except source-specific metadata ownership;
16. MiniShell FS append/sync error paths;
17. QMX receive-safe CAT exact bytes;
18. no forbidden TX CAT strings in JS8Chat production source;
19. finite --slots shutdown;
20. q/input shutdown when input service present;
21. startup cleanup on Audio/CAT/log failures;
22. repeated launch/quit releases Audio/Serial/File handles.

## Linux integration tests

Reuse the real MiniShell process/app-loading path.

Hardware-free integration may use:

- deterministic WAV MiniShell Audio endpoint for timing/content fixtures;
- PTY Serial endpoint for CAT;
- mocked/fake time service where existing harness allows;
- Linux filesystem sandbox for log append.

Do not require ALSA hardware in CTest.

Existing Linux ALSA provider tests remain the provider gate.

## Manual pc-1 / QMX acceptance

After supervisor review, real hardware acceptance is required.

1. identify QMX ALSA capture name:

       arecord -l
       arecord -L

2. identify QMX CDC node;
3. choose a real JS8 dial frequency and pass it explicitly;
4. start the MiniShell JS8Chat monitor with QMX Audio + optional CAT + log;
5. verify CAT moves QMX to the requested dial and never keys RF;
6. observe at least one real HB/CQ or other JS8 decode;
7. run for at least 20 consecutive slots;
8. verify no decode-induced Audio discontinuity;
9. inspect JSONL and verify real event UTC/audio/RF fields;
10. if a multi-frame directed message is heard, verify MESSAGE output/log; absence of one is not a failure;
11. quit cleanly;
12. launch again and prove Audio/CDC/log resources reopen.

Manual evidence should record the actual ALSA endpoint, CDC endpoint, dial Hz, run duration/slots, number of decoded events, and discontinuity count.

## WebSDR relationship

T063/T061 already support aligned WebSDR WAV monitoring/logging.

T064 does not add WebSDR networking.

After T064, both practical RX sources are covered:

    WebSDR -> aligned WAV -> js8_decode logger
    QMX    -> live MiniShell Audio -> js8chat logger

## Architectural constraints

- receive only;
- public MiniShell services only;
- no direct ALSA/POSIX/tty/thread API in JS8Chat app code;
- no raw-slot audio buffering;
- no heap inside js8_engine;
- bounded live state;
- exact T056-T063 protocol semantics;
- same activity schema;
- no FT8 code dependency from JS8Chat;
- no UI redesign;
- Normal mode only.

## Non-goals

Do NOT implement TX, tune, JSC TX, heartbeat auto-ACK, station/reachability DB, activity browser UI, WebSDR network client, PostgreSQL, log rotation, ADIF, other JS8 modes, or ADV hardware acceptance.

## Acceptance criteria

- [x] MiniShell `js8chat` receive-only app target exists
- [x] live RX uses public MiniShell Audio service
- [ ] Linux `alsa:` endpoint works through existing provider (wired and provider tests pass; real QMX pending)
- [x] JS8 app contains no ALSA/POSIX/termios direct dependency
- [x] 12k stereo -> 6k mono frontend is chunk-boundary invariant
- [x] every successful live frontend chunk is UTC-referenced and backdated like MiniFT8
- [x] slot scheduling uses fresh timed chunk positions, not a free-running startup anchor
- [x] cross-slot cumulative sample-count drift is prevented by repeated UTC re-anchoring
- [x] exact 90000-sample live slot geometry
- [x] exactly 89280 samples / 93 blocks decoded per slot
- [x] final 720 samples skipped
- [x] discontinuity causes full timing/frontend/reassembly resync
- [x] capture remains responsive while decode runs
- [x] no multi-second raw-audio slot buffering
- [x] existing multi-signal candidate capacity preserved
- [x] T062 reassembly unchanged
- [x] T063 activity event/schema unchanged
- [x] live MiniShell FS JSON is schema/escaping compatible with host logger
- [x] optional explicit dial gives exact RF mHz
- [x] optional QMX CAT sends only receive-safe startup sequence
- [x] no TX/RX/TA/TM CAT strings in JS8Chat T064 production
- [x] finite slots and operator quit clean up all resources
- [x] no-log mode works
- [x] hardware-free Linux integration tests pass
- [x] all existing T052-T063 tests remain green
- [x] Linux full CTest passes
- [x] portable CTest passes
- [x] boundary/no-heap checks pass
- [x] ASan/UBSan passes
- [x] ADV build remains green
- [x] git diff --check passes
- [x] no unrelated cleanup

Manual hardware gate:

- [ ] real pc-1/QMX Audio open/start/read
- [ ] optional real QMX CAT sync
- [ ] no RF transmit/key
- [ ] at least one real on-air JS8 decode
- [ ] at least 20 consecutive slots monitored
- [ ] no decode-induced Audio discontinuity
- [ ] JSONL real activity verified
- [ ] clean quit and repeated reopen

## Branch workflow

Use:

    codex/T064-js8-live-qmx-rx

Codex:

1. read AGENTS.md, T061-T063, MiniFT8 Audio/timing/QMX ownership, and Linux Audio provider;
2. add receive-only `js8chat` MiniShell app target;
3. implement JS8-owned Audio/frontend/live-slot pieces above public services;
4. reuse exact JS8 engine/protocol/reassembly/activity modules;
5. refactor JSON serialization only enough to share one schema/escaping implementation between host and MiniShell FS sinks;
6. add receive-safe optional QMX CAT;
7. make capture/decode scheduling bounded and responsive without raw-slot buffering;
8. add hardware-free service/timing/discontinuity/CAT/log tests;
9. run all full gates;
10. set Status to REVIEW;
11. document the per-chunk UTC timing implementation, decode-worker architecture, and measured timing;
12. push one reviewable commit and return SHA;
13. no PR and no Actions wait.

## Codex implementation notes

### Implementation summary

Implemented against amended main `244779fc6a622ebed93621a4710c606d32011cc1`.
The Linux `js8chat` MiniShell app provides receive-only Audio capture, a pure
stereo/decimation frontend and timed slot scheduler, bounded asynchronous PHY
decode, unchanged protocol/reassembly/activity modules, optional receive-safe QMX
CAT, and append-only MiniShell FS JSONL. Shared callback serialization preserves
the host logger's schema and escaping. No task-scope deviations.

### Files changed

- `apps/js8chat/main/js8chat_main.c`: CLI, public-service validation, lifecycle,
  capture loop and cleanup.
- `apps/js8chat/src/live_rx/`: frontend, UTC backdating, timed scheduler, bounded
  capture/decode handoff, semantic dispatch, FS resource/log sinks and QMX CAT.
- `platform/linux/linux_js8chat.c`: accepted platform-owned pthread composition.
- `apps/js8chat/src/activity_json/` and `tools/js8_activity_log.[ch]`: one shared
  pure JSON serializer, existing FILE sink retained.
- `CMakeLists.txt`, `tests/js8_tests.cmake`, `tests/architecture_rules.py`:
  application/test targets and enforced ownership boundaries.
- `tests/js8_live_test.c`, `tests/js8_live_boundary_test.py`,
  `tests/js8_live_probe.c`, `tests/linux_js8_live.py`: hardware-free timing,
  service, worker, failure-path and real MiniShell integration coverage.
- App README, JS8 activity/protocol docs and this packet: usage and evidence.

### Invariants preserved

No changes to `apps/js8chat/src/js8_engine` or FT8 production code. Candidate
capacity remains 50, with exact payload dedupe reset per slot. T052–T063 PHY,
protocol, JSC callback resource, reassembly and normalized activity semantics
are reused unchanged. Only the existing host logger serialization moved to a
shared callback module; its output and options remain compatible.

Application code uses public MiniShell services only. Native threading is confined
to the Linux composition layer. CAT emits exactly `MD6;FR0;FT0;FA%011u;` and no
other command. No TX, platform audio/tty stack, raw-slot buffering or unbounded
queue was introduced. The dictionary is read lazily through MiniShell FS from
`/flash/js8chat/jsc.dict`; it is not loaded into RAM wholesale.

### Live timing / capture-decode evidence

Every successful read/frontend conversion queries UTC and backdates by the number
of produced 6 kHz samples. Fresh timed positions locate each boundary; sample
counting supplies only the active 89280-sample capture. Startup discards a partial
slot. The 720-sample tail is skipped before finding the next UTC boundary.
Discontinuity resets frontend, timing, monitor and reassembly and invalidates
in-flight decode results by generation.

The deterministic drift regression both advances a timed chunk across a boundary
and repeats tail timing after 90000 samples have already been consumed. Repeating
the old slot's tail does not begin the next capture; the fresh UTC boundary does.
This explicitly rejects an initial-anchor-plus-sample-counter implementation.

Capture owns one monitor and copies only its waterfall to one fixed worker job.
Release/acquire job ownership protects the immutable snapshot and decoded results;
shutdown joins before releasing resources. Busy workers cause an explicit dropped
window, tested with delayed completion. Capture continues servicing Audio during
PHY decode. Semantic publication/log sync occurs on the capture thread.

Measured on this Linux build:

- Monitor workspace: 211312 bytes, including 161076-byte waterfall; alignment 16.
- Separate worker waterfall: 161076 bytes; `sizeof(Js8Live)`: 12968 bytes.
- App Memory allocations total 385371 bytes including 15 bytes alignment slack.
- Raw/converted audio storage: 1024-byte transport, 512-byte frontend output and
  3840-byte partial engine block; no complete raw slot is buffered.
- Shared JSON formatter uses an 8192-byte bounded stack line. Linux worker stack
  and platform/thread overhead are separate from the above app allocations.

A paced synthetic real-process integration run (`ctest -V -R '^linux_js8_live$'`)
measured decode durations 37155–68849 us, maximum serviced-chunk gap 10356 us,
50 candidates per window, zero dropped windows and zero discontinuities. Fixtures
included two repeated-HB slots, three header/Huffman/JSC slots, four simultaneous
streams over two slots, and repeated launch/append. Pacing is 1 ms per 256-frame
read in the test harness, not a claim about real QMX timing. Production diagnostics
report slot, decode duration, maximum serviced-chunk gap, candidates, unique
payloads, dropped windows and Audio discontinuities for hardware acceptance.

### Activity/log compatibility evidence

Integration byte-compares host WAV and live MiniShell JSON after normalizing only
source slot identity fields (`slot_index`, `elapsed_s`, `first_slot`, `last_slot`).
UTC, exact audio/RF milli-Hz, frame content, escaping, order, and completed MESSAGE
content match. Live slot identity is Unix UTC slot number; canonical event UTC is
that boundary, never decode completion time. Repeated invocation appends to the
same file and reopens Audio/Serial/FS handles. Tests cover no-log operation,
write/sync failures and no MESSAGE completion across discontinuity.

### Test evidence

All commands completed successfully on the final implementation:

```sh
cmake -S . -B build-linux
cmake --build build-linux -j8
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure
# 114/114 passed

cmake -S tests/unit -B /tmp/T064-build-unit
cmake --build /tmp/T064-build-unit -j8
ctest --test-dir /tmp/T064-build-unit --output-on-failure
# 27/27 passed

cmake -S . -B /tmp/T064-build-ref -DJS8_A2_1_REFERENCE_WAV="$HOME/projects/js8chat/A_2_1.wav"
cmake --build /tmp/T064-build-ref -j8 --target js8_decode
ctest --test-dir /tmp/T064-build-ref -R 'js8.*reference|js8.*A2.*1' --output-on-failure
# 1/1 passed; external WAV remains untracked by this commit

cmake -S . -B /tmp/T064-build-sanitize -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -g" -DJS8_A2_1_REFERENCE_WAV="$HOME/projects/js8chat/A_2_1.wav"
cmake --build /tmp/T064-build-sanitize -j8 --target minishell js8chat js8_live_probe js8_live_unit js8_rx_unit js8_phy_unit js8_frame_unit js8_protocol_frame_unit js8_compound_unit js8_directed_unit js8_huffman_unit js8_jsc_unit js8_reassembly_unit js8_activity_unit js8_activity_log_unit js8_decode js8_multislot_host_unit
ctest --test-dir /tmp/T064-build-sanitize -R '^(js8_(phy_unit|rx_unit|frame_unit|protocol_frame_unit|compound_unit|directed_unit|huffman_unit|jsc_unit|reassembly_unit|activity_unit|activity_log_unit|activity_json_unit|activity_wav_unit|live_unit|wav_unit|directed_wav_unit|huffman_wav_unit|jsc_wav_unit|multislot_host_unit|multislot_wav_unit|reassembly_wav_unit|A2_1_reference)|linux_js8_live)$' --output-on-failure
# 23/23 passed, including live worker/process integration and resource corruption

. "$HOME/projects/esp-idf/export.sh"
idf.py -C platform/adv build
# passed; image 0x151790 bytes, 78% application partition free

git diff --check
# passed
```

Full Linux/portable suites include dependency/platform boundaries, compiled engine
no-heap checks, all existing JS8 regressions and Linux Audio provider tests.
ASan/UBSan was run outside the restricted sandbox for LeakSanitizer support.

### Manual validation still required

The real pc-1/QMX gate above remains pending **after supervisor review**. Record
actual ALSA/CDC endpoints, dial Hz, at least 20 consecutive slots, event count,
decode/read-gap diagnostics and discontinuity count. Verify no RF keying, real
UTC/audio/RF log fields, clean quit and repeated reopen. No ALSA hardware or
on-air reception claim is made by the synthetic/provider tests.

### Known limitations / risks

One pending decode window is allowed; overload drops/report windows explicitly.
FS sync and semantic formatting run on the capture thread, so real disk/QMX load
must be evaluated using the diagnostics. Live event identities support nonnegative
Unix slots through uint32; the pure UTC helper also tests negative epoch arithmetic.
A missing/corrupt JSC resource fails explicitly when compressed DATA is encountered.
Finite `--slots` counts fully decoded windows; an unpaced WAV can overrun the one-job
handoff (the integration harness deliberately supplies paced Audio and fake UTC).
No ADV live app packaging or hardware acceptance is added.

### Commit

The reviewed implementation commit is:

`3c9430f1b697fec308b0b1b9c4af81051744d3f3`

No PR or GitHub Actions wait.

## Supervisor review

Reviewed commit `3c9430f1b697fec308b0b1b9c4af81051744d3f3` against the amended T064 timing contract and the accepted MiniFT8 live timing model.

Result: **PASS — implementation accepted for real pc-1/QMX testing.**

Review findings:

- One bounded implementation commit, one commit ahead of amended T064 baseline `244779fc6a622ebed93621a4710c606d32011cc1`.
- JS8 engine/protocol/reassembly/activity code remains unchanged; live integration is layered above it.
- Production JS8Chat app code uses only public MiniShell services. Native pthread use is confined to `platform/linux/linux_js8chat.c`.
- Linux ALSA remains fully provider-owned; JS8Chat opens only the public 12 kHz S16 stereo Audio contract.
- Every successful live frontend chunk obtains a fresh MiniShell UTC reference and backdates it by the number of produced 6 kHz samples.
- Slot scheduling uses the fresh timed chunk position. The regression explicitly proves that repeating/perturbing a prior tail does not start the next slot from an old `+90000` counter.
- 90000 samples is used only as exact slot geometry; capture consumes exactly 89280 samples and skips the nominal 720-sample tail.
- Audio discontinuity resets frontend phase, slot scheduling, monitor capture and T062 reassembly, and increments a generation so stale worker output cannot publish.
- Capture owns the live monitor and hands one immutable waterfall snapshot to a bounded Linux decode worker. No whole raw-audio slot is buffered and there is no unbounded queue.
- A busy worker causes an explicit decode-window drop rather than blocking Audio capture or silently overwriting work.
- Worker ownership is synchronized with release/acquire atomics and shutdown joins the worker before state/services are released.
- Candidate capacity and per-slot exact payload dedupe remain the accepted T054/T061 values.
- Live semantic dispatch reuses the accepted T056-T063 decoders and T062 reassembly without introducing a parallel protocol path.
- Shared `js8_activity_json` serialization is used by both the T063 host FILE sink and the new MiniShell FS sink; integration tests byte-compare equivalent JSON after normalizing only source slot-identity fields.
- Live UTC in logs is the RF slot boundary, not decode-completion time. Audio/RF frequencies remain exact integer milli-Hz.
- QMX CAT is receive-safe only and emits exactly `MD6;`, `FR0;`, `FT0;`, `FA%011u;`; no TX/tone/time CAT command exists in the production JS8Chat live source.
- Resource cleanup covers Audio, Serial, log file, JSC dictionary and app Memory; repeated synthetic MiniShell launches prove reopen.
- Synthetic integration exercises repeated HB, mixed Huffman/JSC message reassembly, four simultaneous streams, CAT, FS append, discontinuity invalidation and actual MiniShell app loading.
- Reported gates are consistent with the diff: Linux 114/114, portable 27/27, sanitizer 23/23, external WAV regression, boundary/no-heap checks, ADV build and diff check all pass.

Main was fast-forwarded to the reviewed implementation commit.

T064 now enters TESTING for real pc-1/QMX acceptance.

## Architect test result

Pending real pc-1/QMX acceptance after supervisor review.
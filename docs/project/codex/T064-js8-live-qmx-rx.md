# T064 — Live Linux QMX JS8 RX monitor

Status: READY

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

- [ ] MiniShell `js8chat` receive-only app target exists
- [ ] live RX uses public MiniShell Audio service
- [ ] Linux `alsa:` endpoint works through existing provider
- [ ] JS8 app contains no ALSA/POSIX/termios direct dependency
- [ ] 12k stereo -> 6k mono frontend is chunk-boundary invariant
- [ ] every successful live frontend chunk is UTC-referenced and backdated like MiniFT8
- [ ] slot scheduling uses fresh timed chunk positions, not a free-running startup anchor
- [ ] cross-slot cumulative sample-count drift is prevented by repeated UTC re-anchoring
- [ ] exact 90000-sample live slot geometry
- [ ] exactly 89280 samples / 93 blocks decoded per slot
- [ ] final 720 samples skipped
- [ ] discontinuity causes full timing/frontend/reassembly resync
- [ ] capture remains responsive while decode runs
- [ ] no multi-second raw-audio slot buffering
- [ ] existing multi-signal candidate capacity preserved
- [ ] T062 reassembly unchanged
- [ ] T063 activity event/schema unchanged
- [ ] live MiniShell FS JSON is schema/escaping compatible with host logger
- [ ] optional explicit dial gives exact RF mHz
- [ ] optional QMX CAT sends only receive-safe startup sequence
- [ ] no TX/RX/TA/TM CAT strings in JS8Chat T064 production
- [ ] finite slots and operator quit clean up all resources
- [ ] no-log mode works
- [ ] hardware-free Linux integration tests pass
- [ ] all existing T052-T063 tests remain green
- [ ] Linux full CTest passes
- [ ] portable CTest passes
- [ ] boundary/no-heap checks pass
- [ ] ASan/UBSan passes
- [ ] ADV build remains green
- [ ] git diff --check passes
- [ ] no unrelated cleanup

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

### Files changed

### Invariants preserved

### Live timing / capture-decode evidence

### Activity/log compatibility evidence

### Test evidence

### Manual validation still required

### Known limitations / risks

### Commit

## Supervisor review

## Architect test result

Pending real pc-1/QMX acceptance after supervisor review.
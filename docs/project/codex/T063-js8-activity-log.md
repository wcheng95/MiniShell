# T063 — JS8 RX activity events and append-only host logger

Status: READY

## Architect intent

Turn the working RX protocol stack into a practical band-monitoring tool before any TX work.

T062 reconstructs standard directed free-text messages. T063 adds a normalized pure RX activity-event model and an append-only JSONL host logger so the same downstream representation can later be fed by aligned WebSDR WAVs, live QMX/UAC audio, and later ADV live RX.

This task does not add live QMX/UAC capture yet. It creates the common event/logging boundary first.

## Accepted baseline

- T052-T060: PHY and protocol/content RX COMPLETE.
- T061: aligned multi-slot WAV decode COMPLETE.
- T062: standard directed free-text reassembly COMPLETE.

Read AGENTS.md, docs/js8/architecture.md, docs/js8/application-protocol.md, T061/T062, and the existing FT8 log/radio ownership patterns under apps/ft8/src/log_service and apps/ft8/src/radio_control.

## Pure normalized activity model

Add a pure C module under apps/js8chat/src/js8_engine, e.g. js8_activity.[ch].

Required event kinds:

    JS8_ACTIVITY_HEARTBEAT
    JS8_ACTIVITY_CQ
    JS8_ACTIVITY_COMPOUND
    JS8_ACTIVITY_DIRECTED
    JS8_ACTIVITY_DATA
    JS8_ACTIVITY_MESSAGE

A DATA event distinguishes codec: none / huffman / jsc.

Common bounded fields should cover:

    kind
    slot_index
    elapsed_seconds
    frequency_millihz
    tx_flags
    score
    hard_errors

Semantic fields should cover the already-decoded information needed by each kind:

    call / from / to
    grid
    command_code and canonical command identity
    beacon/CQ subtype
    codec
    text fragment
    completed message first_slot / last_slot

Use existing callsign/grid capacities. DATA text must handle current JSC fragment size; MESSAGE text must handle the T062 1024-byte message. No heap.

The activity module performs no monitor/DSP work, no protocol decoding, no JSC lookup, no file I/O, no clock access, no JSON formatting, and no platform calls. Caller supplies already-decoded semantics.

## Frequency ownership

Preserve exact integer audio frequency from T062 as frequency_millihz.

Host logger may optionally accept:

    --dial-hz <integer>

When present:

    rf_frequency_millihz = dial_hz * 1000 + frequency_millihz

Do not reconstruct RF frequency through floating point. If dial frequency is absent, omit RF frequency from JSON rather than treating audio Hz as RF Hz.

## Time ownership

Aligned multi-slot WAV already has exact elapsed time:

    elapsed_seconds = slot_index * 15

Add optional host metadata:

    --start-utc YYYYMMDDTHHMMSSZ

Example:

    --start-utc 20260923T050000Z

Requirements:

- strict fixed-format UTC parser;
- reject seconds not divisible by 15;
- compute each slot UTC by exact integer seconds;
- no local-time/timezone conversion;
- no system clock required;
- do not infer UTC from file mtime.

If omitted, JSON contains slot/elapsed time but no fake absolute UTC.

## Host JSONL logger

Extend js8_decode with an optional append-only activity log for aligned multi-slot mode.

Suggested invocation:

    ./build-linux/js8_decode --all-slots --messages --log-jsonl activity.jsonl --dial-hz 14078000 --start-utc 20260923T050000Z capture.wav

Logging may also work without --messages; then frame-level events are logged but no completed MESSAGE events are produced.

Do not change stdout/stderr when no logging option is supplied.

## JSONL schema

One JSON object per event, one line each.

Every line includes:

    "schema":"js8-activity-v1"
    "event":...
    "slot":...
    "elapsed_s":...
    "audio_millihz":...
    "tx_flags":...
    "score":...
    "hard_errors":...

When available add:

    "utc":"2026-09-23T05:00:15Z"
    "dial_hz":14078000
    "rf_millihz":...

Semantic fields are emitted only when relevant.

Heartbeat/CQ:

    call, grid, beacon

Compound:

    call, grid, extra, bits3

Directed:

    from, to, command_code, command, optional number, free_text/ack/end73 flags

DATA:

    codec, text

MESSAGE:

    from, to, first_slot, last_slot, text

Activity JSONL is an observation log, not ADIF/QSO logging.

## JSON escaping

Use one strict host JSON-string writer for all text.

Escape at least quote, backslash, and control bytes 0x00..0x1f. Serialize Latin-1 bytes >=0x80 deterministically as \u00XX so output remains valid ASCII/UTF-8 JSON. Test quote, backslash, NUL, newline, tab, DEL, and non-ASCII Latin-1.

## Append-only durability

- open with append semantics;
- never truncate preexisting content;
- write exactly one complete JSON line per event;
- flush after each decoded slot;
- close on cleanup;
- log open/write/flush failure produces deterministic nonzero exit after preserving already printed decode output;
- no rotation in T063.

## Event ordering and dedupe

Preserve T061 unique-payload order within each slot.

For a payload that also completes a T062 message:

1. write its frame-level event first;
2. then write the MESSAGE event.

Do not globally sort or add another dedupe layer. Identical payloads in different slots remain separate events. MESSAGE appears only on T062 COMPLETE.

## Host output compatibility

Without logger options these must remain byte-for-byte unchanged:

    js8_decode file.wav
    js8_decode --all-slots file.wav
    js8_decode --all-slots --messages file.wav

T060-T062 reference tests remain authoritative.

## Synthetic logger tests

Cover at least:

1. heartbeat;
2. CQ FIELD;
3. ACK directed event;
4. free-text DIRECTED FIRST;
5. Huffman DATA;
6. JSC DATA_COMPRESSED;
7. completed mixed-codec MESSAGE;
8. repeated heartbeat in two slots -> two events;
9. four simultaneous streams -> all frame events and correct MESSAGE events;
10. gapped/incomplete reassembly -> raw events but no MESSAGE;
11. orphan DATA -> DATA event but no MESSAGE;
12. exact dial + audio -> RF milli-Hz;
13. no dial -> no RF field;
14. exact UTC increments;
15. invalid UTC syntax;
16. UTC seconds not divisible by 15;
17. four-slot one-minute timestamps;
18. JSON escaping of all required byte classes;
19. append preserves preexisting file contents;
20. injected logger write/flush failure;
21. no-log invocation remains byte-for-byte T062 compatible.

## Pure activity tests

Pure unit tests require no WAV/filesystem/clock. Cover every event kind, bounds/capacity, exact byte preservation, invalid input canaries, and no hidden heap/state. Portable CTest includes the module.

## Documentation

Update docs/js8/application-protocol.md and preferably add docs/js8/activity-log.md describing js8-activity-v1.

Record explicitly:

- RX subset is now sufficient to emit normalized activity;
- frequency/time ownership;
- JSONL host logger is an observation tool;
- activity log is not ADIF/QSO log;
- TX remains deliberately deferred.

## Architectural constraints

- pure C normalized event model;
- no heap/platform/fs/time in js8_engine;
- JSON/file/UTC parsing outside engine;
- append-only host log;
- no radio control;
- no live audio source;
- no TX;
- no FT8 changes.

## Non-goals

Do NOT implement live QMX audio capture, QMX CAT in JS8Chat, WebSDR network client, HTTP/WebSocket fetching, database/PostgreSQL storage, log rotation, ADIF/QSO logging, station reachability database, activity UI, automatic HB ACK, TX, or other JS8 modes.

## Acceptance criteria

- [ ] pure normalized RX activity model added
- [ ] heartbeat/CQ/compound/directed/DATA/message represented
- [ ] exact integer audio frequency preserved
- [ ] optional dial produces exact RF milli-Hz
- [ ] optional aligned UTC start produces exact per-slot UTC
- [ ] JSONL v1 schema documented
- [ ] strict JSON escaping handles arbitrary Latin-1 bytes
- [ ] append-only host logger implemented
- [ ] existing log content never truncated
- [ ] deterministic event ordering
- [ ] no logger-level dedupe
- [ ] existing stdout/stderr unchanged without logger options
- [ ] repeated events across slots retained
- [ ] incomplete/orphan streams never fabricate MESSAGE
- [ ] mixed Huffman/JSC MESSAGE logged exactly
- [ ] logger failures deterministic/nonzero
- [ ] T062 reassembly unchanged
- [ ] T061 multi-slot unchanged
- [ ] T060 A_2_1 reference unchanged
- [ ] js8_engine platform-free/no-heap
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

## Next task boundary

After T063, preferred next step is live Linux QMX RX monitoring:

    QMX UAC input
      -> JS8 slot decoder
      -> normalized activity events
      -> same JSONL logger

CAT/dial-frequency ownership should reuse the already-proven MiniFT8 Serial/QMX architecture without changing the event/log schema.

## Branch workflow

Use:

    codex/T063-js8-activity-log

Codex:

1. read AGENTS.md, T061/T062, JS8 protocol docs, and FT8 logging ownership pattern;
2. implement pure activity event model first;
3. add host JSONL serializer/logger separately;
4. add optional --dial-hz and --start-utc metadata;
5. preserve all existing stdout/stderr without logging options;
6. log raw semantic frame events plus completed MESSAGE events;
7. add append/error/escaping/time/frequency tests;
8. do not add live QMX/WebSDR networking/TX;
9. run full JS8 gates;
10. set Status to REVIEW;
11. fill implementation/test evidence;
12. push one reviewable commit and return SHA;
13. no PR and no Actions wait.

## Codex implementation notes

### Implementation summary

### Files changed

### Invariants preserved

### Activity schema / logger evidence

### Test evidence

### Manual validation still required

### Known limitations / risks

### Commit

## Supervisor review

## Architect test result

No required hardware/RF acceptance. Optional aligned WebSDR JSONL experiment after review.
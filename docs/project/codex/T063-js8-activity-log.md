# T063 — JS8 RX activity events and append-only host logger

Status: COMPLETE

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

- [x] pure normalized RX activity model added
- [x] heartbeat/CQ/compound/directed/DATA/message represented
- [x] exact integer audio frequency preserved
- [x] optional dial produces exact RF milli-Hz
- [x] optional aligned UTC start produces exact per-slot UTC
- [x] JSONL v1 schema documented
- [x] strict JSON escaping handles arbitrary Latin-1 bytes
- [x] append-only host logger implemented
- [x] existing log content never truncated
- [x] deterministic event ordering
- [x] no logger-level dedupe
- [x] existing stdout/stderr unchanged without logger options
- [x] repeated events across slots retained
- [x] incomplete/orphan streams never fabricate MESSAGE
- [x] mixed Huffman/JSC MESSAGE logged exactly
- [x] logger failures deterministic/nonzero
- [x] T062 reassembly unchanged
- [x] T061 multi-slot unchanged
- [x] T060 A_2_1 reference unchanged
- [x] js8_engine platform-free/no-heap
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

Implemented and tested the pure normalized model first, then added a separate
host JSONL adapter. The model builds bounded owned snapshots from already-decoded
facts and exact text bytes. The host logs successful semantic frame events and,
when enabled, T062 COMPLETE events in existing receive order. Optional dial/UTC
metadata uses integer arithmetic exclusively. No scope deviations.

Reviewed JS8 ownership/protocol docs, T061/T062, and FT8 `log_service` /
`radio_control` ownership contracts. No FT8 implementation was modified or
imported; filesystem/UTC/JSON work remains in the explicit host tool.

### Files changed

- `apps/js8chat/src/js8_engine/js8_activity.[ch]`: bounded normalized event model.
- `apps/js8chat/tools/js8_activity_log.[ch]`: append adapter, strict ASCII JSON,
  Gregorian UTC parsing/formatting, checked integer dial parsing, error latching.
- `apps/js8chat/tools/js8_decode.c`: logger CLI, semantic event creation,
  frame-before-MESSAGE ordering, per-slot flush and cleanup/error reporting.
- `tests/js8_activity_test.c`: pure bounds, kinds, codecs, bytes and canaries.
- `tests/js8_activity_log_test.c`, `tests/js8_activity_log_test.py`: host calendar,
  overflow, injected I/O errors, JSON round-trip and append tests.
- `tests/js8_reassembly_wav_test.py`: optional activity checks over existing
  message fixtures plus beacon/compound/metadata/failure fixtures.
- `tests/js8_tests.cmake`, `CMakeLists.txt`: portable and host test registration.
- `tests/architecture_rules.py`: narrow `fopen` exception for the new authorized
  host logger file; no engine restriction or generic checker was relaxed.
- `docs/js8/activity-log.md`, `docs/js8/application-protocol.md`: schema,
  ownership, invocation, ordering and durability limits.
- This task packet: implementation and gate evidence.

### Invariants preserved

The engine contains no allocation, platform, file, clock, JSON or codec lookup
operations in the activity module. Caller-provided semantics are validated and
copied without text conversion. Audio frequency stays signed integer milli-Hz;
elapsed seconds uses 64-bit `slot * 15`. Metadata never turns audio into fake RF
or supplies fake UTC when absent.

No PHY/codec/reassembly behavior, T061 geometry/dedupe, JSC resource, FT8, public
MiniShell API, live source, radio control, UI, TX or ADIF change. Existing decode
stdout/stderr and no-log CLI error handling are retained. T062 output was compared
byte-for-byte against the accepted T062 binary in default, all-slots and messages
modes on A_2_1; representative no-log usage/error diagnostics also match (with only
executable-path spelling normalized for usage comparisons).

### Activity schema / logger evidence

`js8-activity-v1` is documented in `docs/js8/activity-log.md`. Events are HB, CQ,
COMPOUND, DIRECTED, DATA, MESSAGE. CQ FIELD retains its canonical beacon identity.
Raw compound-directed observations use COMPOUND plus `compound_directed:true`;
no association/command semantics are invented. Failed codec output is not logged
as text. MESSAGE common diagnostics refer to its completing frame.

Synthetic fixtures verify heartbeat/CQ FIELD, plain/raw-directed compound, ACK,
numbered directed content, FIRST free-text, Huffman/JSC DATA, exact mixed MESSAGE,
repeated frames in separate slots, four simultaneous streams, gap/orphan/overflow
absence of MESSAGE, frame-before-MESSAGE order, exact bin/sub-bin frequency,
dial addition, UTC rollover and four-slot timestamps. Each logging run compares
stdout/stderr with the same no-log invocation. No-message logging omits MESSAGE,
and no-metadata logging omits UTC/dial/RF fields.

Strict JSON tests round-trip all 256 Latin-1 values, including quote, backslash,
NUL, newline, tab, DEL and high bytes in a 1023-byte MESSAGE. Output remains ASCII.
Repeated invocation preserves an existing JSON line and appends two complete
events. Injected write, flush and close failures latch the first error. Real host
open failure and `/dev/full` return nonzero after retaining complete decode stdout
and original diagnostics, then append the deterministic logger error diagnostic.

The pinned external WAV remains blob
`d986a4e5a9cc654dffbfadae73ec35cc9cea1d83` and is not committed. Exact real-WAV log
with `--dial-hz 14078000 --start-utc 20260923T050000Z`:

```json
{"schema":"js8-activity-v1","event":"DATA","slot":0,"elapsed_s":0,"audio_millihz":556250,"tx_flags":2,"score":26,"hard_errors":15,"utc":"2026-09-23T05:00:00Z","dial_hz":14078000,"rf_millihz":14078556250,"codec":"jsc","text":"MSG ID 416"}
```

It is orphan DATA in this capture; no MESSAGE is fabricated.

Measured Linux sizes: `Js8Activity` 1160 bytes, `Js8ActivityFields` 120 bytes.
The activity object file has text=1123, data=0, bss=0 bytes. The host serializer
uses one bounded 8192-byte line buffer; the engine adds no heap or global state.

### Test evidence

```sh
cmake -S . -B build-linux
cmake --build build-linux -j8
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --repeat until-pass:3 --output-on-failure
ctest --test-dir build-linux --repeat until-pass:3 --output-on-failure
ctest --test-dir build-linux -R '^linux_serial_unit$' --repeat until-pass:20 --output-on-failure
ctest --test-dir /tmp/T061-build-ref -R '^linux_serial_unit$' --output-on-failure
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --repeat until-pass:20 --output-on-failure

cmake -S tests/unit -B /tmp/T063-build-unit
cmake --build /tmp/T063-build-unit -j8
ctest --test-dir /tmp/T063-build-unit --output-on-failure

cmake -S . -B /tmp/T063-build-ref -DJS8_A2_1_REFERENCE_WAV="$HOME/projects/js8chat/A_2_1.wav"
cmake --build /tmp/T063-build-ref -j8 --target js8_decode
ctest --test-dir /tmp/T063-build-ref -R "js8.*reference|js8.*A2.*1" --output-on-failure

PYTHONDONTWRITEBYTECODE=1 python3 tests/app_dependency_boundary.py . js8chat
PYTHONDONTWRITEBYTECODE=1 python3 tests/app_platform_boundary.py . js8chat

cmake -S . -B /tmp/T063-build-sanitize -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -g" -DJS8_A2_1_REFERENCE_WAV="$HOME/projects/js8chat/A_2_1.wav"
cmake --build /tmp/T063-build-sanitize -j8 --target js8_rx_unit js8_phy_unit js8_frame_unit js8_protocol_frame_unit js8_compound_unit js8_directed_unit js8_huffman_unit js8_jsc_unit js8_reassembly_unit js8_activity_unit js8_activity_log_unit js8_decode js8_multislot_host_unit
ctest --test-dir /tmp/T063-build-sanitize -R '^js8_(phy_unit|rx_unit|frame_unit|protocol_frame_unit|compound_unit|directed_unit|huffman_unit|jsc_unit|reassembly_unit|activity_unit|activity_log_unit|activity_json_unit|activity_wav_unit|wav_unit|directed_wav_unit|huffman_wav_unit|jsc_wav_unit|multislot_host_unit|multislot_wav_unit|reassembly_wav_unit|A2_1_reference)$' --output-on-failure
ctest --test-dir /tmp/T063-build-sanitize -R '^js8_activity_wav_unit$' --output-on-failure

source ~/projects/esp-idf/export.sh
idf.py -C platform/adv build
git diff --check
```

Final results:

- Linux full CTest: 111/111 passed; every test passed on its first attempt in
  the final full run despite the retry allowance. Earlier runs hit the existing
  `linux_serial_unit` line 67 PTY timeout assertion, including outside the
  sandbox. The accepted T061 binary reproduces it; focused current-binary retry
  passed on attempt two. No serial code/tests were changed.
- Portable CTest: 25/25 passed.
- External A_2_1 reference: 1/1 passed, exact legacy output.
- Boundary/no-heap checks: passed. Initial full/portable runs identified the
  missing host-file `fopen` registration; added only that authorized exception.
- ASan/UBSan: 21/21 passed, including JSC resource corruption and logger/error
  tests. Final strengthened integer-frequency assertion also passed the targeted
  sanitized WAV test. Sanitizers ran outside the sandbox for LeakSanitizer.
- ADV: passed, image `0x151790` bytes, 78% app partition free; no live integration.
- `git diff --check`: passed.

During focused test development, corrected a test's hand-calculated maximum dial
value and mistaken compound-vector indices; production input validation was not
weakened. All final assertions pass.

### Manual validation still required

None required. Optional architect aligned WebSDR JSONL experiment after review.
No RF/hardware, PR or GitHub Actions wait gate.

### Known limitations / risks

T061 alignment and T062 bounded association limits still apply. Gregorian years
are 0001..9999; a later slot beyond that range is a log error. File flush means
stdio flush, not fsync or transactional durability. Short OS writes can leave a
partial final line; append-only operation does not repair prior bytes or promise
concurrent-writer atomicity. Existing files should end in LF. No rotation,
database, ADIF, live source, CAT, UI or TX is included.

### Commit

The reviewed implementation commit is:

`28f741d0a5111af968301bfb30fc2b6073cae559`

No PR or GitHub Actions wait.

## Supervisor review

Reviewed commit `28f741d0a5111af968301bfb30fc2b6073cae559` against T063.

Result: **PASS**.

Review findings:

- One bounded implementation commit, one commit ahead of the T063 baseline.
- `js8_activity` is a pure bounded snapshot layer: no heap, file, clock, JSON, platform, monitor/DSP, or codec/resource ownership.
- Event kinds cover HB, CQ, COMPOUND, DIRECTED, DATA and completed MESSAGE, with exact byte preservation for DATA/MESSAGE text.
- Common audio frequency remains signed integer milli-Hz and elapsed time is exact `slot_index * 15` in 64-bit arithmetic.
- Host dial metadata is parsed as checked integer Hz with headroom for signed 32-bit audio offsets; RF milli-Hz is formed without floating point.
- UTC metadata uses strict `YYYYMMDDTHHMMSSZ`, validates Gregorian dates, requires 15-second alignment, and is advanced by exact integer slot elapsed time.
- JSONL schema is versioned as `js8-activity-v1`; missing metadata is omitted rather than fabricated.
- One JSON string writer handles quotes, backslashes, controls, DEL and all Latin-1 high bytes; bytes >=0x80 are emitted as `\u00XX`, keeping logs valid ASCII JSON.
- Logger opens in append mode, writes one bounded line per event, flushes per slot, latches the first I/O error, and never truncates existing content.
- Event ordering is causal: frame-level event first, then MESSAGE completion when T062 returns COMPLETE.
- No logger-level dedupe is added; T061 per-slot payload dedupe remains authoritative and repeated events across slots are retained.
- Host event construction reuses already-decoded T056-T062 semantics rather than duplicating protocol decoding.
- Logger failure paths preserve already-produced decode stdout and return nonzero with a deterministic logger diagnostic.
- No-log invocation and stdout/stderr remain byte-for-byte compatible with accepted T060-T062 behavior.
- Synthetic coverage includes HB/CQ FIELD, compound/directeds, Huffman/JSC DATA, mixed MESSAGE, repeated slots, four streams, gaps/orphans, metadata omission/presence, UTC rollover, RF frequency, append preservation and injected I/O failures.
- Existing JS8 PHY/content/reassembly, FT8, MiniShell API, JSC resource, and ADV production behavior are unchanged.
- Reported gates are consistent with the diff: Linux 111/111, portable 25/25, sanitizer 21/21, real-WAV regression, boundary/no-heap checks, ADV build, and diff check all pass. The documented serial PTY intermittency is unrelated and no serial code/test changed.

Main was fast-forwarded to the reviewed implementation commit.

## Architect test result

No required hardware/RF acceptance. Optional aligned WebSDR JSONL experiment remains available; task complete.
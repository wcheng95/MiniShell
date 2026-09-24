# JS8 RX activity JSONL (T063)

The implemented RX subset emits normalized, bounded semantic activity snapshots.
The pure `js8_activity` model owns the supplied bytes and metadata; it does not
perform protocol decoding, DSP, dictionary access, file I/O, time conversion, or
JSON formatting. DATA supports current Huffman/JSC capacities; completed MESSAGE
text has the T062 1024-byte capacity including NUL. All storage is caller-owned.

The host logger is an observation tool, not an ADIF/QSO log or conversation store.
TX remains deliberately deferred. There is no live audio, CAT, network, database,
UI, or automatic reply integration in this task.

## Invocation and metadata ownership

```sh
./build-linux/js8_decode --all-slots --messages \
  --log-jsonl activity.jsonl --dial-hz 14078000 \
  --start-utc 20260923T050000Z capture.wav
```

`--log-jsonl` requires aligned `--all-slots`. `--messages` remains optional and
must follow `--all-slots`; without it only frame events are logged. Metadata
options require `--log-jsonl`. All options precede the WAV filename. Existing
stdout/stderr are unchanged without logging options; successful logging also
leaves decode output unchanged.

Audio frequency is the exact signed integer milli-Hz derived from candidate
bin/sub-bin fields. A caller-supplied nonnegative decimal integer `--dial-hz`
adds `dial_hz` and `rf_millihz = dial_hz * 1000 + audio_millihz`, using integer
arithmetic. The maximum dial value is 9223372034707292, reserving signed 64-bit
headroom for any signed 32-bit audio frequency. Without dial metadata both fields
are omitted; audio offset is never presented as RF frequency.

Elapsed time is always `slot * 15` seconds from aligned PCM sample zero.
`--start-utc` accepts exactly `YYYYMMDDTHHMMSSZ`, Gregorian years 0001..9999,
valid calendar dates, hours 00..23, minutes 00..59, and seconds 00/15/30/45.
No local-time conversion, system clock, leap-second inference, or file mtime is
used. UTC is computed by adding integer elapsed seconds. A result beyond year
9999 is a deterministic `utc_range` log failure. Without this option `utc` is
omitted.

## Schema `js8-activity-v1`

Each line is one ASCII JSON object followed by LF. Required common fields:

| Field | Meaning |
| --- | --- |
| `schema` | `js8-activity-v1` |
| `event` | `HB`, `CQ`, `COMPOUND`, `DIRECTED`, `DATA`, or `MESSAGE` |
| `slot`, `elapsed_s` | Aligned zero-based slot index and elapsed integer seconds |
| `audio_millihz` | Exact signed audio frequency |
| `tx_flags` | Final three transmission flags, independent of application class |
| `score`, `hard_errors` | Candidate score and decoded payload LDPC hard errors |

Optional common fields are `utc` (`YYYY-MM-DDTHH:MM:SSZ`), `dial_hz`, and
`rf_millihz`, present only with the respective supplied metadata.

| Event | Additional fields |
| --- | --- |
| HB / CQ | `call`, `grid`, `beacon`, `subtype` (0..7); CQ FIELD has `event:"CQ"`, `beacon:"CQ FIELD"` |
| COMPOUND | `call`, `grid`, `extra`, `bits3`, boolean `compound_directed` |
| DIRECTED | `from`, `to`, `command_code`, canonical `command` (including spaces), optional `number`, booleans `free_text`, `ack`, `end73` |
| DATA | `codec` (`none`, `huffman`, `jsc`), `text` |
| MESSAGE | `from`, `to`, `first_slot`, `last_slot`, `text` |

Unresolved compound-directed content stays raw: `compound_directed:true`, empty
`grid`, and uninterpreted `extra`/`bits3`. No compound association is inferred.
Unavailable grids are empty strings. DATA events require successful text decode;
codec/resource failures retain existing raw diagnostics but do not invent text.
The host currently emits `huffman` or `jsc`; `none` is available in the normalized
model. MESSAGE is emitted only on T062 COMPLETE. Its slot, flags, score and hard
errors describe the completing frame, and its audio offset is the final matched
stream frequency. Gapped/expired/overflow/orphan streams produce no MESSAGE.

One JSON string writer handles all strings. Quote/backslash are escaped;
control bytes 0x00..0x1f, DEL, and Latin-1 0x80..0xff use `\u00xx` escapes.
Explicit text lengths preserve embedded NUL. No UTF-8 interpretation is applied
to decoded Latin-1 bytes.

## Ordering and file behavior

Frame events follow the existing per-slot unique-payload candidate order. A
completing frame's event precedes its MESSAGE event. No sorting or new dedupe
occurs; repeated payloads in different slots remain separate observations.

The logger opens with binary append semantics, constructs a complete bounded
line before writing, flushes after every decoded slot, and closes on cleanup.
Existing bytes are never truncated or repaired (the caller should supply an
empty file or a log ending in LF). Open/write/flush/close failures latch the first
error and return a nonzero process exit after preserving decode output, with
`activity log error: <reason>` on stderr. Subsequent event writes stop after a
failure. An OS short write can leave a partial final line; append-only operation
does not promise transactional rollback, fsync durability, or concurrent-writer
atomicity. Rotation is out of scope.

## Live MiniShell sink (T064)

`js8chat --rx <Audio endpoint> [--dial-hz hz] [--cat <Serial endpoint>]
[--log <MiniShell path>] [--slots N]` uses the same formatter and escaping through
a write callback. The host FILE adapter remains append/flush; the app opens
MiniShell FS with WRITE|CREATE|APPEND, syncs after each published decode slot and
closes at shutdown. File errors are explicit, with no rotation or ADIF.

Live `slot` is the actual nonnegative Unix UTC slot ID (uint32); `elapsed_s` is
that ID times 15, consistent with the activity model. `utc` is that slot boundary,
not decode-completion time. The formatter's metadata origin is the Unix epoch.
Audio/RF fields remain exact integer milli-Hz. Negative UTC slot IDs are rejected
by the live event path; the pure timing conversion tests cover negative epochs.

T066 adds optional `--rx-delay-ms N` (integer 0..5000, default zero). Source age
is subtracted from each fresh UTC reference before sample backdating, so slot/UTC
fields identify the corrected RF slot. JSON schema and audio/RF arithmetic are
unchanged. Browser/WebSDR routing belongs to the operator; use no local CAT for
that path. See the app README for Linux `pulse:` monitor setup.

Every successful Audio chunk gets a new MiniShell UTC reference after frontend
conversion. The reference is backdated by the produced 6 kHz sample count, and
its absolute position locates each target slot's capture start 9600 samples
(1.6 s) before the UTC boundary. MiniFT8's initial 240-sample (40 ms) tolerance
selects the first target. A scheduled start stepped over by a chunk begins at the
first available sample. Counting fills exactly 89280 samples / 93 blocks, then
fresh UTC positions locate the following pre-roll point; no free-running clock
or sample padding is used. Audio discontinuity resets
frontend, timing, monitor and reassembly and invalidates stale worker output.

The RX-only app and platform-worker arrangement are described in
[the app README](../../apps/js8chat/README.md). Real QMX validation remains a
separate hardware gate; synthetic timing/content tests do not replace it.

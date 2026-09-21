# T048 — Mini-CW transcript logging + safe annotation shortcut

Status: READY

## Baseline

Start from the accepted colored Mini-CW baseline:

```text
golden/minicw-color-ui-baseline
53cd7f45e1b7464cde159955263d8f553f02093d
```

T042/T043/T044A/T045/T046/T047 behavior is frozen unless explicitly changed
below. In particular, clean Tone/audio and the colored UI remain protected.

Pinned standalone Mini-CW reference:

```text
wcheng95/Mini-CW
3bfbf169b7c2d49a1be3e9a4c80f945edb32033e
```

Reference transcript behavior is in `components/app_core/app_core.c` and
`components/storage_service/storage_service.c`.

## Architect requirements

1. **No `G [...]` records.**
2. **Transcript only.** Do not add ADIF, Cabrillo, formatted QSO records, QSO
   summaries, callsign-triggered records, or any second log format.
3. Daily filename:
   ```text
   /flash/minicw/YYYYMMDD.txt
   ```
   using MiniShell UTC, analogous to Mini-FT8's daily `YYYYMMDD.txt`.
4. Preserve the standalone Mini-CW transcript style, except for removing all
   `G [...]` behavior.
5. Add the Keyer-normal shortcut **`"`** (ASCII double quote) as a temporary
   safe annotation mode:
   - first press saves current KeyOut and Mute;
   - runtime KeyOut becomes `OFF`;
   - runtime Mute becomes `OFF`;
   - second press stops/discards any remaining annotation playback, then restores
     the saved KeyOut and Mute exactly.
   The purpose is to enter extra transcript information such as a band without
   keying the transmitter.

No PR and no hardware testing by Codex.

## Transcript format

Use the pinned V1.2 transcript line exactly:

```text
T [YYYYMMDD HHMMSS][x.xxx] <transcript>
```

If the minute transcript exceeds the V1.2 1024-character text buffer, preserve
the V1.2 truncation marker:

```text
 [TRUNC]
```

The `[x.xxx]` field remains the V1.2 placeholder. Do not synthesize CAT
frequency or band metadata. The annotation shortcut is how the operator may add
manual information such as `20M`.

There must be **no product code path that emits `G [`**.

Example:

```text
T [20260921 184203][x.xxx] CQ POTA DE AG6AQ ... 20M ...
```

## What feeds the transcript

Preserve V1.2 Keyer transcript semantics:

- decoded paddle/SK characters;
- decoded word spaces;
- TX keyboard characters;
- M1..M5 text when appended;
- automatic M1 repeats;
- normal inserted spaces;
- backspace correction of the still-current minute buffer.

The transcript is one chronological text stream. It does not distinguish RX/TX,
does not create QSO objects, and does not add operator/callsign records.

The `"` shortcut itself is consumed as control and is never appended to TX or
the transcript.

## Minute grouping and timestamps

Preserve the V1.2 minute bucket model:

- the first accepted transcript character starts a minute buffer and captures
  `YYYYMMDD HHMMSS`;
- characters in the same UTC minute append to that buffer;
- when UTC minute changes, finalize the previous minute record and begin a new
  record on the next character;
- the filename for a finalized record is derived from the **stored record
  timestamp date**, not the later filesystem-flush time. This is required for
  correct midnight rollover.

Because MiniShell apps can exit, unlike the standalone infinite loop, T048 must
also finalize/flush the current transcript on normal app shutdown.

If MiniShell UTC is unavailable, Keyer operation continues normally; a new
timestamped transcript record must not be fabricated with a fake date/time.

## Daily persistence

Persistence is append-only:

```text
/flash/minicw/YYYYMMDD.txt
```

For each finalized line:

1. ensure `/flash/minicw` exists as needed;
2. open daily file with WRITE | CREATE | APPEND;
3. write the entire canonical line plus `\n`, handling short writes;
4. sync;
5. close exactly once.

No whole-file read/rewrite is needed for transcript append.

Filesystem failures are nonfatal to Keyer operation. Do not poison Tone, GPIO,
input, or the application lifecycle because a log record cannot be stored. Do not
retry a close-ambiguous record in a way that can duplicate it.

## Audio/timing protection: deferred filesystem writes

Do **not** perform FAT write/sync in the CW timing/audio path.

Transcript capture and minute rollover happen in RAM immediately. Finalized
minute records waiting for disk are queued in application-owned MiniShell Memory.

Drain queued records only when the Keyer is safely idle, using the same kind of
conditions already used to protect settings persistence:

- no pending text TX;
- no active text TX;
- no remaining TX text;
- no M1 repeat active/waiting;
- no Tune;
- no Tune output;
- Audio not busy;
- paddle/SK input lines inactive.

At normal shutdown, stop/cancel Keyer audio/output first as required below, then
drain the current transcript and queued records synchronously while Filesystem is
still alive.

Mini-CW is not RAM-bounded. Dynamic queue storage through the MiniShell Memory
service is allowed. Native/libc heap calls remain outside the portable app
boundary.

If queue allocation fails, drop that finalized record cleanly and continue Keyer
operation. Never corrupt or partially publish another record.

## Safe annotation mode — `"`

This is a temporary runtime overlay, not a persisted setting.

### Enter

The shortcut is active only in the normal Keyer view, not inside Operation/text
editing.

A first `"` press may enter annotation mode only from a safe idle state. It
must not interrupt an existing real transmission, Tune, pending TX, active M1
repeat, or active paddle/SK element. If busy, consume the shortcut without
changing KeyOut/Mute.

On successful entry:

1. save the current runtime `keyer_key_out_mode_t`;
2. save current runtime Mute;
3. mark annotation mode active;
4. set runtime KeyOut to `KEYER_KEY_OUT_OFF`;
5. set runtime Mute to `false` / `OFF`.

The existing header naturally shows `OFF` for KeyOut.

While active, ordinary supported keyboard characters/macros continue through the
normal Mini-CW TX/text path. Therefore:

- they are added to the transcript exactly like ordinary TX text;
- local CW sidetone remains audible because Mute is OFF;
- physical KeyOut lines remain inactive because KeyOut is OFF.

Do not add a special log marker merely for entering/exiting annotation mode.

### Persistence isolation

The temporary `KeyOut=OFF` must **never** become the persisted KeyOut setting.

The persistence snapshot must continue to represent the saved logical KeyOut
while annotation mode is active, or persistence must otherwise explicitly ignore
this temporary overlay.

Mute is currently runtime-only but must still be restored exactly.

Any normal KeyOut/Mute UI change while annotation mode is active must not defeat
the OFF/OFF safety overlay. It is acceptable to reject/ignore those two changes
while the overlay is active.

### Exit

A second `"` press exits annotation mode immediately.

Before restoring KeyOut:

1. cancel M1 repeat state;
2. cancel pending TX;
3. clear the TX FIFO;
4. stop any active annotation Tone/playback through the existing clean cancel/stop
   path;
5. ensure KeyOut is released while it is still OFF.

Only then:

6. restore the saved KeyOut mode;
7. restore the saved Mute value;
8. clear annotation-mode state;
9. refresh the UI.

This ordering is mandatory: no queued annotation tail may become RF after the
saved KeyOut is restored.

The second `"` does not force a new transcript line; minute grouping remains
unchanged.

### Shutdown while active

If the app exits while annotation mode is active, perform the same safe exit
sequence **before** the normal settings snapshot/save. Then finalize and drain the
transcript log. The temporary OFF value must never be written to `setting.txt`.

## Ownership

```text
Mini-CW app/domain
  owns:
  - transcript semantics
  - minute grouping
  - annotation-mode state and save/restore policy
  - when a finalized record is safe to persist

Mini-CW storage service
  owns:
  - /flash/minicw/YYYYMMDD.txt path construction
  - append record policy

Mini-CW private port
  owns:
  - MiniShell Filesystem handles
  - MiniShell Memory allocation
  - MiniShell UTC access

MiniShell
  owns:
  - UTC source
  - Filesystem implementation
  - Memory implementation/lifecycle
```

No direct ESP-IDF/FATFS/board calls in Mini-CW domain modules.

## Explicit non-goals

Do not implement:

- `G [...]` GPS/grid logging;
- ADIF;
- Cabrillo;
- formatted QSO logs;
- QSO list/view UI;
- CAT/frequency discovery;
- automatic band inference;
- separate annotation records;
- logging for trainer modes;
- runtime log viewer/editor;
- log deletion/rotation policy.

This task is Keyer transcript persistence only.

## Tests

### Reference transcript behavior

Prove:

- decoded Keyer characters/spaces append;
- typed TX characters append;
- M1..M5 append;
- automatic M1 repeats append;
- backspace removes the current buffered character;
- CR/LF normalization remains V1.2-compatible if exercised internally;
- minute transition creates separate `T [...]` records;
- timestamp is the first-character timestamp of each minute buffer;
- 1024-character overflow produces exactly one ` [TRUNC]` suffix;
- no `G [` record is emitted anywhere.

### Daily files

Use deterministic UTC and prove:

- `2026-09-21` writes `/flash/minicw/20260921.txt`;
- midnight rollover sends the old minute to `20260921.txt` and the new minute
  to `20260922.txt`;
- append uses CREATE|APPEND, full-write loop, sync, close;
- pre-existing file content is preserved;
- open/write/short-write/sync/close failures are nonfatal to Keyer;
- ambiguous close failure is not automatically retried/duplicated.

### Deferred-write/audio boundary

Prove:

- minute rollover while Tone/TX is active performs **zero filesystem writes**;
- finalized record is queued in MiniShell Memory;
- once idle, queued records are drained in order;
- multiple continuous busy minutes retain ordering;
- queue allocation failure drops only that record and leaks no Memory allocation;
- app-end cleanup has no live queued allocations;
- no filesystem append occurs inside Tone enqueue/hold/timing callbacks.

### Annotation shortcut

Prove at minimum:

1. starting from `KeyOut=SKN, Mute=ON`, first `"` saves those values and
   runtime becomes `KeyOut=OFF, Mute=OFF`;
2. starting from another valid KeyOut/Mute combination restores that exact pair;
3. `"` itself is not logged and not added to TX text;
4. a typed annotation such as `20M` is logged through the ordinary transcript
   path;
5. KeyOut GPIO never asserts during annotation playback;
6. local Tone remains active/audible path because Mute is OFF;
7. exit with annotation playback still pending clears/stops it **before** saved
   KeyOut is restored;
8. there is no RF/keyOut tail after restore;
9. entering while real TX/Tune/repeat/paddle activity is busy does not alter
   KeyOut or Mute;
10. temporary `KeyOut=OFF` never changes serialized `setting.txt`;
11. shutdown while annotation mode is active restores the saved values before
    settings snapshot/save;
12. manual KeyOut/Mute setting actions cannot defeat the active safety overlay.

### Regression/freeze

Full Linux CTest, portable units, focused Mini-CW/storage/Tone/architecture
checks, real ADV build, clean Mini-CW ELF build/inspection, `git diff --check`.

Protected audio source/header diff must remain NONE. Color UI, separator,
callsign lookup, fixed header, Keyer timing, persistence format and sole
`mini_api_get` resident import remain unchanged except for the authorized
transcript behavior.

Report resident BIN/SRAM and external ELF deltas.

## Hardware acceptance

On ADV:

1. perform a short normal Keyer exchange and confirm a
   `/flash/minicw/YYYYMMDD.txt` file appears;
2. inspect it and confirm only `T [...]` transcript records, no `G [...]`;
3. verify normal paddle and M1 audio remain clean/no-pop;
4. press `"`: header KeyOut becomes `OFF`;
5. type `20M`: local CW is heard, transmitter/keyOut does not key;
6. press `"` again: saved KeyOut/Mute return;
7. confirm no note tail keys RF after restore;
8. confirm `20M` appears in the transcript;
9. Ctrl+C exits normally and the final partial minute is flushed.

## Branch

Use:

```text
codex/T048-minicw-transcript-log
```

Return exact implementation SHA, changed-file list, tests, memory/ELF/resource
evidence, and any remaining hardware-only validation.

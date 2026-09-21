# T044A — Mini-CW Keyer persistence, audio-frozen

Status: READY

## Safety baseline

The hardware-validated golden checkpoint is:

```text
main: 48a40d79c13ed60ef9f8444a060164852d226fcd
recovery branch: golden/minicw-clean-audio
```

Hardware truth at this checkpoint:

```text
MiniShell -> minicw.elf
paddle        clean / no pop
automatic M1  clean / no pop
sound         matches standalone MiniCW_V1_2.bin
```

If T044A causes any audible regression, stop. Do not debug persistence and audio
simultaneously. The architect will revert to the golden checkpoint.

## Objective

Add persistent Mini-CW **Keyer-mode settings only** through the existing
MiniShell Filesystem API.

T044A is persistence-only. UTC/time work is deferred to a separate T044B if it is
still useful after T044A hardware acceptance.

No resident MiniShell functionality is required for T044A.

## Absolute audio freeze

The validated audio path is frozen.

T044A MUST NOT modify:

```text
include/minishell/api.h
core/minishell_services/audio_service.c
platform/common/tone_stream.c
platform/common/tone_stream.h
platform/common/tone_sim.c
platform/common/tone_sim.h
platform/adv/adv_audio_speaker.cpp
apps/minicw/src/audio_service/audio_service.c
```

Do not change:

- Tone API semantics;
- worker task/priority/stack;
- 64-segment ring;
- 5 ms chunks;
- envelope/DDS/phase behavior;
- startup prime;
- codec/I2S setup;
- DMA geometry;
- `esp_codec_dev_write()` transport;
- busy accounting;
- Mini-CW key timing;
- display refresh cadence.

The resident ADV firmware should remain byte-for-byte unchanged from the golden
baseline. If the firmware BIN changes, treat that as a blocker unless the only
difference is proven build nondeterminism; do not accept an unexplained resident
delta.

## Scope

Add an application-owned persistence service under:

```text
apps/minicw/src/storage_service/
```

All actual filesystem operations must use MiniShell Filesystem through the
existing private Mini-CW port boundary. No POSIX/FATFS/stdio file access.

Canonical path:

```text
/flash/minicw/setting.txt
```

Temporary path:

```text
/flash/minicw/setting.tmp
```

Directory:

```text
/flash/minicw
```

Do not touch `/flash/config.txt` or the existing `/flash/keyer/` settings.

## Persisted settings

Persist only the Keyer-relevant settings that the pinned standalone Mini-CW
actually persisted.

### [system]

```text
volume
tone_hz
key_in
key_in_wpm
```

### [keyer]

```text
key_out
paddle
sk_wpm
tx_delay_s
tune_timeout_s
repeat_interval_s
mycall
m1
m2
m3
m4
m5
```

Do **not** persist:

- mute — session-only in the pinned Mini-CW behavior;
- date/time;
- GPS baud;
- USB drive state;
- trainer/lesson/word/callsign/plaintext settings;
- GPIO line assignments.

The canonical file should therefore be a small Keyer-only subset of the pinned
Mini-CW `setting.txt` vocabulary, for example:

```text
# Mini-CW Keyer settings

[system]
volume=80
tone_hz=700
key_in=Paddle
key_in_wpm=19

[keyer]
key_out=SK
paddle=IambicA
sk_wpm=19
tx_delay_s=0
tune_timeout_s=10
repeat_interval_s=6
mycall=AG6AQ
m1=CQ POTA
m2=
m3=
m4=
m5=
```

Use the pinned Mini-CW labels/ranges, not the existing `apps/keyer` labels.

## Load policy

Loading must happen before the resident tone owner is opened, so flash reads do
not occur while the continuous audio worker is active.

Recommended initialization order:

```text
construct defaults
load /flash/minicw/setting.txt into a local snapshot
initialize Mini-CW services
apply loaded snapshot
initialize/open audio using the loaded pitch/volume
initialize UI
```

Equivalent ordering is acceptable if it guarantees that startup filesystem I/O
finishes before Tone open.

Missing file:

- use compiled golden defaults;
- launch normally;
- do not force a startup write merely to create the file.

Unknown sections/keys:

- ignore them for forward compatibility.

Malformed known value:

- reject the loaded snapshot as a whole;
- keep compiled defaults;
- launch normally with a short UI status such as `Settings invalid`;
- do not automatically rewrite the bad file.

Filesystem read error other than NOT_FOUND:

- use defaults;
- launch normally;
- surface `Settings read failed`;
- do not rewrite automatically.

Do not let a bad settings file prevent the user from reaching the known-good
Keyer.

## Value rules

Preserve pinned Mini-CW limits and labels.

At minimum:

```text
volume             0..99
tone_hz            300..999
key_in_wpm         5..60
sk_wpm             5..60
tx_delay_s         0..99
tune_timeout_s     0..20
repeat_interval_s  1..99
mycall             <= KEYER_MYCALL_MAX_LEN, A-Z/0-9/'/'
m1..m5             <= KEYER_MESSAGE_MAX_LEN, printable ASCII
```

Modes use the Mini-CW KeyIn/KeyOut/Paddle label vocabulary from the pinned
reference.

## Save policy — protect real-time audio

Runtime edits take effect immediately in memory.

Persistence must be **deferred to a quiet point**. Do not perform flash writes
from the immediate UI-setting event path while CW/Tune is active.

Mark settings dirty when a persisted field changes. Save only when:

- no automatic TX is active;
- no automatic TX is pending/queued;
- M1 repeat is not in an active/waiting cycle;
- Tune/straight-key hold is inactive;
- `audio_service_is_busy()` is false.

A small post-change quiet/debounce interval is allowed and encouraged.

If a setting changes again before the save, persist only the latest complete
snapshot.

The persistence state machine must not change Mini-CW key timing or audio
scheduling semantics.

At clean application exit, after CW has been stopped, make one best-effort flush
of a dirty snapshot before relinquishing app resources. A save failure must not
prevent output-release cleanup.

## Transactional write

Use MiniShell Filesystem only.

Required sequence:

```text
mkdir /flash/minicw          (OK or EXISTS)
remove stale setting.tmp     (best effort before create)
open setting.tmp CREATE|TRUNC|WRITE
write complete canonical snapshot, handling short writes
sync setting.tmp
close setting.tmp
rename setting.tmp -> setting.txt
```

On any failure:

- preserve the previous `setting.txt`;
- close any open handle;
- remove the temporary file where possible;
- retain the in-memory runtime setting;
- surface `Save failed`;
- retry only on a later explicit settings change or clean exit, not in a tight
  foreground loop.

Do not delete the existing destination before the commit rename.

Follow the established MiniShell rename contract used by current application
configuration services.

## Application integration

Prefer using existing Mini-CW getters/setters and config-copy functions.

Do not modify `keyer_service` timing/state-machine logic merely to add
persistence. If a missing non-timing accessor is truly required, stop and report
before expanding scope.

The storage service owns:

- parsing;
- validation;
- canonical serialization;
- dirty snapshot tracking policy helpers if useful.

`app_core` remains the coordinator deciding when a setting change should be
persisted and when a quiet-point save may run.

The private port may gain generic Filesystem wrappers because it is already the
only MiniShell API owner for this external app. Do not alter its existing Tone
wrapper semantics.

## Tests

### Storage unit tests

Cover at least:

- missing file -> defaults, no startup write;
- canonical valid file loads every persisted field;
- partial valid file overlays defaults;
- unknown keys/sections ignored;
- malformed known numeric/mode/mycall/message value rejects whole snapshot;
- boundary values;
- short reads;
- short writes;
- read/open/write/sync/close/rename failures;
- prior destination remains intact on failed save;
- stale temp cleanup;
- canonical round-trip;
- M1-M5 values containing spaces and additional '=' after the first separator
  survive round-trip.

### Integration tests

Prove:

- setting edits apply immediately in memory;
- edits mark persistence dirty;
- no filesystem write occurs while automatic TX is active/pending;
- no filesystem write occurs during M1 repeat wait;
- no filesystem write occurs during Tune/hold;
- no filesystem write occurs while Audio reports busy;
- one save occurs after the system becomes quiet;
- latest snapshot wins after multiple edits;
- clean exit attempts one final dirty save;
- save failure never prevents KeyOut release / Tone cleanup;
- loaded volume/pitch are applied without changing the Tone API sequence.

### Regression gates

Run:

- full Linux CTest;
- portable units;
- Mini-CW focused tests;
- architecture/dependency/platform checks;
- real ADV firmware build;
- clean external `minicw.elf` build;
- external import inspection;
- `git diff --check`.

The external ELF must still import only `mini_api_get`.

## Binary/audio guard evidence

Before handoff, explicitly compare the protected audio files against
`48a40d79c13ed60ef9f8444a060164852d226fcd`.

Report:

```text
protected audio file diff: NONE
resident firmware BIN:     identical / explain if not
resident SRAM delta:       0 expected
minicw.elf delta:          record exact size
```

Do not claim audio safety solely from tests.

## Hardware acceptance

After supervisor diff review:

1. launch `minicw`;
2. confirm loaded settings are visible/effective;
3. change WPM/volume/pitch/M1, exit, relaunch, confirm persistence;
4. verify a failed/invalid settings file falls back safely;
5. paddle must remain **clean/no pop**;
6. automatic M1 must remain **clean/no pop**;
7. Tune/straight key must remain clean;
8. Ctrl+C must return silent;
9. repeated launch/exit must remain clean.

Any audible regression is an immediate T044A failure. Revert to:

```text
golden/minicw-clean-audio
48a40d79c13ed60ef9f8444a060164852d226fcd
```

Do not attempt audio tuning inside T044A.

## Non-goals

Do not:

- add UTC/time display or editing;
- add GPS;
- add trainer modes;
- add battery/sleep;
- add USB MSC;
- modify the public MiniShell API;
- modify resident Tone/audio implementation;
- change Keyer UI design;
- change Keyer timing;
- migrate old standalone FATFS files automatically;
- modify existing `apps/keyer` or MiniFT8.

## Branch

Use:

```text
codex/T044A-minicw-persistence
```

No PR and no hardware testing by Codex.

Set T044A to REVIEW, push one bounded implementation commit, and return the exact
SHA plus test/resource/binary guard evidence.

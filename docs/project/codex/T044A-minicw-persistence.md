# T044A — Mini-CW Keyer persistence, audio-frozen

Status: REVIEW

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


## Implementation handoff

Implementation baseline: `083d1b6eaff704d151a0be3cede919c9b176b5c2` on
`codex/T044A-minicw-persistence`. Golden audio checkpoint:
`48a40d79c13ed60ef9f8444a060164852d226fcd`. The baseline resident source/build
inputs are unchanged from that checkpoint. Commit reference: the single
implementation commit containing this handoff; exact SHA returned after push.

### Implementation summary / files changed

- `apps/minicw/src/storage_service/storage_service.{c,h}` adds allocation-free
  defaults, Keyer-only snapshot parsing/validation, canonical serialization and
  equality comparison. Reads are bounded to 4,095 bytes, lines to 159 bytes;
  oversized input and embedded NUL are rejected. All known values are validated
  before publishing any loaded snapshot. Unknown sections/keys are ignored.
  Pinned mode labels, case-insensitive aliases and numeric mode aliases are
  accepted; mycall remains strictly A–Z/0–9/slash. M1–M5 retain literal spaces,
  printable punctuation and all `=` characters after the first separator.
  Whole-line `#`/`;` comments are accepted; inline comments are not stripped from
  message data. Missing/invalid/unreadable files never trigger startup writes.
- `apps/minicw/src/port/minicw_port.{c,h}` adds generic private text-file read and
  transactional replacement wrappers. Every operation uses MiniShell Filesystem.
  The save sequence is mkdir (OK/EXISTS), best-effort stale-temp removal,
  CREATE|TRUNC|WRITE, complete short-write loop, sync, close, rename. The existing
  destination is never removed. Failures close the handle and clean the temporary
  path where possible. Storage errors do not latch the application's fatal
  hardware/service error state.
- `apps/minicw/src/app_core/app_core.{c,h}` coordinates load/apply, complete
  snapshot change detection and deferred saves. All startup reads/close finish
  before the existing Tone open. The frozen audio initializer still opens at
  700 Hz / 80; existing setters then apply loaded volume/pitch before the first
  Keyer update. Neither its private wrapper nor its API sequence is redesigned.
  Runtime edits apply immediately. Saves require 250 ms quiet with no active,
  pending or queued automatic TX, M1 repeat cycle/wait, Tune, asserted physical
  input (including muted straight key), or Audio busy. Existing config-copy
  accessors include stabilized SK WPM; no timing/accessor changes were needed.
  Failure displays `Save failed` and suppresses automatic retries until another
  persisted change or clean exit. Invalid/read-error status is present in the
  first displayed frame. Mute and non-Keyer settings are not persisted.
- Clean exit snapshots settings **before** shutdown changes KeyOut to OFF, then
  uses the existing CW stop/Tone close. One dirty save attempt follows successful
  Tone close while Filesystem is still available, before remaining app resources
  are released. Save failure cannot block KeyOut release or Tone cleanup. Fatal
  service/cleanup exits do not attempt an additional filesystem flush.
- `apps/minicw/sources.cmake`, `tests/minicw_tests.cmake` and
  `tests/architecture_rules.py` register the storage module and its no-heap,
  private-port-only dependencies. `tests/minicw_fs_fake.h` supplies a short-I/O,
  fault-injectable MiniShell Filesystem fixture. Existing runtime tests now use
  that fixture so their original UI assertions remain valid with a missing
  settings file. `tests/minicw_persistence_test.c` adds the storage/coordinator/
  cleanup cases; all accepted domain/runtime assertions remain.
- `platform/adv/elf_apps/minicw/minicw.ld` now also pads `.rodata` to four bytes.
  The first external inspection caught the new settings strings leaving it at
  1,877 bytes, misaligning the loader's following packed section. Padding to
  1,880 fixes that existing loader requirement without changing resident code
  or weakening the inspector. This is external-app packaging only.
- `apps/minicw/README.md` documents current settings behavior and ownership.

### Behavior / invariants preserved

No public API, resident production source, Tone wrapper, audio implementation,
Keyer timing/state machine, UI input/render implementation, existing `keyer`,
MiniFT8, USB ownership, DMA geometry or task configuration changed. The existing
Mini-CW scheduling loop and display refresh calls are retained; persistence runs
only at the guarded quiet point. No heap allocation, platform file calls, new
worker or resident capability was added. No audio tuning or T044B work occurred.

### Tests run and results

```sh
cmake -S . -B build-linux
cmake --build build-linux -j8
ctest --test-dir build-linux --output-on-failure
# PASS: 83/83
cmake -S tests/unit -B /tmp/T044A-unit
cmake --build /tmp/T044A-unit -j8
ctest --test-dir /tmp/T044A-unit --output-on-failure
# PASS: 25/25
ctest --test-dir build-linux -R minicw --output-on-failure
# PASS: 5/5 (domain, runtime, persistence, dependency/platform boundaries)
ctest --test-dir build-linux -R 'tone|minicw|architecture|boundary' --output-on-failure
# PASS: 18/18
python3 tests/app_dependency_boundary.py . minicw
python3 tests/app_platform_boundary.py . minicw
# PASS

source /home/wei/projects/esp-idf/export.sh
idf.py -C platform/adv build
# PASS: real ADV firmware (before and after implementation)
idf.py -C platform/adv/elf_apps/minicw fullclean
idf.py -C platform/adv/elf_apps/minicw elf
# PASS: final clean 1074-step external build
python3 tests/minicw_elf_inspect.py platform/adv/elf_apps/minicw/build/minicw.app.elf
# PASS: sole resident import mini_api_get; 595 mapped relocations; packed alignment valid
xtensa-esp32s3-elf-readelf -rW platform/adv/elf_apps/minicw/build/minicw.app.elf
xtensa-esp32s3-elf-size -A platform/adv/build/minishell_adv.elf
xtensa-esp32s3-elf-size -A platform/adv/elf_apps/minicw/build/minicw.app.elf
cmp /tmp/T044A-baseline.bin platform/adv/build/minishell_adv.bin
# PASS: byte-for-byte identical
git diff --check
# PASS
```

An intermediate full Linux run during clean ELF compilation hit the unchanged
serial PTY filled-output-queue timeout assertion at `linux_serial_test.c:67`.
The final full rerun passed 83/83; serial code/tests were not changed. As in
T042/T043, `readelf` reports the packaging's removed `.dynamic` section while
printing relocations; the independent inspector verifies imports and relocation
mapping successfully.

New coverage includes: missing/default/no-write startup; full and partial loads;
unknown keys/sections; numeric boundaries and malformed values/modes/mycall/
message text; oversize/NUL input; message length limits; spaces/additional equals
round-trip; short reads/writes; open/read/write/zero-progress/sync/close/rename
failures (including failure after partial I/O); intact previous destination;
stale-temp cleanup; immediate volume/pitch/WPM edits; every quiet guard and
muted physical hold; latest snapshot winning; one quiet save; no mute-only save;
no retry loop; retry on later edit/exit; first-frame invalid/read-failure status;
no bad-file rewrite; read completion before Tone open; loaded audio settings;
final flush after Tone close and before resource release; failed final save still
releasing both KeyOut lines and Tone. Existing M1/repeat/Tune/UI timing traces and
all frozen audio algorithm/worker regressions pass unchanged.

### Explicit binary / audio guard evidence

Each of the eight task-listed protected files was compared byte-for-byte with
`git show 48a40d79c13ed60ef9f8444a060164852d226fcd:<path>`:

```text
protected audio file diff: NONE
resident firmware BIN:     IDENTICAL
resident SRAM delta:       0 bytes
external resident imports: mini_api_get only
```

Baseline and final local resident BIN are both **1,381,280 bytes**, SHA-256:

```text
051b656ad85111c2427ef4348b1ff012f0b26ade5a935a05f159cad8b706cb9b
```

The baseline was built before editing using resident inputs identical to the
hardware-accepted golden checkpoint. Both builds used ESP-IDF v5.5.4 / Xtensa
GCC 14.2.0. No build-nondeterminism exemption was needed.

| Resident section | Golden-source baseline | T044A | Delta |
| --- | ---: | ---: | ---: |
| `.iram0.text` | 63,959 | 63,959 | 0 |
| `.dram0.data` | 27,016 | 27,016 | 0 |
| `.dram0.bss` | 40,336 | 40,336 | 0 |
| Static internal SRAM | — | — | **0** |

| External `minicw.app.elf` | Baseline | T044A | Delta |
| --- | ---: | ---: | ---: |
| File bytes | 38,820 | 40,388 | +1,568 |
| `.text` | 24,052 | 27,568 | +3,516 |
| `.rodata` | 1,272 | 1,880 | +608 |
| `.data` | 1,176 | 1,224 | +48 |
| `.bss` | 2,740 | 3,268 | +528 |
| Section-loader text + data allocation | 29,240 | 33,940 | +4,700 |

Other final sections: `.hash` 40, `.dynsym` 80, `.dynstr` 38, `.rela.dyn` 7,140,
`.rela.plt` 12, `.eh_frame` 44, `.got` 4; `size -A` total 41,298. File delta and
loaded-section delta differ because of external ELF file/segment alignment.
No app heap allocation is introduced. Startup uses a bounded 4,096-byte read
buffer plus parser/snapshot stack locals; save uses a 1,024-byte serialization
buffer. These use the existing foreground stack, whose size is unchanged.
Resident SRAM/worker/queue allocations are unchanged; loaded external app memory
is accounted separately above. No hardware stack high-water measurement was made.

### Hardware/manual validation still required / risks

No hardware testing or PR was performed. Supervisor review comes before the
packet's persistence and paddle/M1/Tune/exit acceptance. Passing software gates
and identical resident firmware are not a substitute for that acoustic check.
Runtime filesystem work is admitted only after observed quiet; a new physical
press during a synchronous filesystem operation cannot be predicted. On any
audible regression, revert to `golden/minicw-clean-audio` at
`48a40d79c13ed60ef9f8444a060164852d226fcd`; no audio debugging/tuning belongs in
T044A. Missing or malformed settings remain recoverable, and failed saves leave
runtime edits active but not durably committed until a later successful retry.

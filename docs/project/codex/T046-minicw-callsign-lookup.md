# T046 — Mini-CW callsign -> operator-name lookup

Status: READY


## Hardware correction addendum — 2026-09-21

Hardware acceptance exposed a real T046 loader defect. This addendum supersedes the
original whole-file ~4 KiB CSV-read allowance while preserving every other T046
ownership/audio/UI constraint.

Observed on ADV:

```text
/flash/minicw/setting.txt      opens/reads normally
/flash/minicw/qsocalls.csv     opens/reads normally after remount/recreate
actual V1.2 qsocalls.csv       10,788 bytes / 814 lines
small (<4 KiB) qsocalls.csv    lookup works on hardware
full 10,788-byte file          Mini-CW reports "Lookup unavailable"
```

The pinned standalone V1.2 repository contains the same reference database shape:

```text
wcheng95/Mini-CW
3bfbf169b7c2d49a1be3e9a4c80f945edb32033e
examples/fatfs/qsocalls.csv
10,788 bytes
814 lines
```

Root cause in T046:

```c
char text[4096];
minicw_port_file_read("/flash/minicw/qsocalls.csv", text, sizeof(text));
```

The private whole-file helper rejects input that cannot fit in 4,095 payload bytes.
The real V1.2 database therefore maps to `STORAGE_OP_FAILED` and the UI status
`Lookup unavailable`.

### Required correction

Keep this correction inside T046 and on the existing branch. Do not create T047.

Replace only the callsign-table whole-file load with a bounded streaming read path.

Required architecture:

```text
Mini-CW storage_service
    owns CSV line assembly, validation, parsing and 192-entry policy
        |
        v
Mini-CW private port
    owns opaque MiniShell Filesystem open/read/close handles
        |
        v
MiniShell Filesystem API
```

Do not expose `mini_file_t` or the public MiniShell API outside the private port.
A small opaque Mini-CW read-stream handle/seam is acceptable.

Streaming requirements:

- no heap;
- keep `MINICW_OP_ENTRY_CAP == 192`;
- keep the existing 3,648-byte static operator table;
- use only small bounded read/line buffers (128/256-byte scale);
- support records split across arbitrary filesystem read boundaries;
- support LF and CRLF;
- trim/validate rows exactly as T046 already specifies;
- skip overlong/malformed rows and resume at the next line;
- preserve first-valid-row duplicate precedence;
- continue scanning after 192 stored entries so a later valid row produces
  `STORAGE_OP_TRUNCATED`;
- no total CSV file-size limit;
- a NUL/read/open/close failure remains nonfatal to Keyer startup and must not
  leave a partially trusted table attached;
- close the file on every exit path;
- all callsign-file I/O must still complete before Tone opens;
- no runtime reload;
- do not change `setting.txt` loading/persistence.

The existing small whole-file helper may remain for `setting.txt`; do not enlarge
its 4 KiB buffer merely to make callsign lookup pass.

### Regression requirement

Add a regression representing the actual pinned V1.2 database scale:

```text
10,788 bytes
814 CSV lines
```

It may use the pinned fixture itself or a deterministic fixture with exactly that
size/line count and valid V1.2-schema rows. It must prove:

- the loader reads past 4 KiB successfully;
- all input is consumed/closed before Tone open;
- only the first 192 valid rows are stored;
- the result is `STORAGE_OP_TRUNCATED`, not `STORAGE_OP_FAILED`;
- lookup from retained rows still works.

Retain the existing short-read, malformed-row, open/read/close-failure, startup
ordering, domain-recognition and UI-priority tests.

### Freeze remains absolute

Do not modify resident MiniShell production code or the protected Tone/audio path
for this correction. In particular the following remain frozen:

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

Expected correction evidence:

```text
protected audio diff = NONE
fixed header         = unchanged
resident BIN         = identical
resident SRAM delta  = 0
sole ELF import      = mini_api_get
```

Run the original T046 full gates again and append the new exact commit SHA/results
to this packet. Status returns to REVIEW after the correction commit is pushed.
Hardware acceptance is then repeated with the full 10,788-byte V1.2 file.


## Audited baseline

Start from the post-migration ownership baseline:

```text
main production baseline:
00d540baef94c2f5b36511818c9d3f728a909846

recovery:
golden/minicw-audited-baseline
```

Architecture contract:

```text
docs/MiniCW/baseline-audit.md
```

Hardware truth at this baseline:

```text
persistence       PASS
T045 UI/I/O       PASS
paddle audio      clean / no pop
automatic M1      clean / no pop
```

## Objective

Restore the useful standalone Mini-CW V1.2 callsign -> operator-name lookup
without weakening the audited MiniShell / Mini-CW boundary.

The pinned standalone reference is:

```text
wcheng95/Mini-CW
3bfbf169b7c2d49a1be3e9a4c80f945edb32033e
```

Existing MiniShell `minicw` already contains:

- callsign candidate recognition;
- base-call extraction for slash calls;
- own-call exclusion;
- 72/73 clearing behavior;
- operator-name state;
- `keyer_service_get_op_name()`.

T046 adds only the missing bounded table load / population and a lower-line UI
display. Do not redesign the recognizer.

## Absolute audio / resident freeze

T046 is external-app-only.

Do not modify:

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

Do not modify resident MiniShell production code.

Resident firmware must remain byte-for-byte identical to the audited baseline.

## Ownership

Mini-CW owns:

- CSV schema;
- callsign/name validation;
- table capacity policy;
- callsign recognition and lookup;
- name display policy.

MiniShell owns:

- Filesystem handles and storage backend;
- application lifecycle/resource reclamation.

ADV owns actual flash/FAT implementation.

All I/O must go through the private Mini-CW port over MiniShell Filesystem.

## No heap

The audited Mini-CW app is no-heap.

Standalone V1.2 used `realloc()` to grow the lookup table. Do **not** port that
allocation behavior.

Use a bounded static table in the external app.

Recommended capacity:

```text
MINICW_OP_ENTRY_CAP = 192
```

With the current:

```text
KEYER_OP_CALL_MAX_LEN = 6
KEYER_OP_NAME_MAX_LEN = 11
```

this costs about 3.6 KiB of external app BSS and zero resident SRAM.

Do not introduce malloc/calloc/realloc/free.

## File

Canonical path:

```text
/flash/minicw/qsocalls.csv
```

Format follows pinned Mini-CW:

```text
call,name
K6ABC,Alice
W1XYZ,Bob
```

Rules:

- optional first/header row `call,name`;
- blank lines ignored;
- lines beginning with `#` or `;` ignored;
- one comma separates call and name;
- additional comma in the name makes that row invalid;
- trim surrounding spaces/tabs around both fields;
- call length 1..6;
- name length 1..11;
- callsign characters: A-Z / 0-9 only;
- normalize loaded callsigns to uppercase;
- name is printable ASCII and preserves case;
- malformed rows are skipped, not fatal;
- duplicate calls retain standalone behavior: first matching loaded row wins.

Missing file is normal:

- lookup table count = 0;
- Keyer launches normally;
- do not create a file automatically;
- do not write flash at startup.

An unreadable/oversized file must not prevent Keyer startup.

Use a bounded read/parser design. Do not increase foreground stack materially
beyond T044A's already-proven 4 KiB settings read buffer. A maximum file payload
around 4 KiB is acceptable for T046.

If more than 192 valid rows are present, keep the first 192 and expose a short
nonfatal status such as `Lookup truncated`.

## Startup ordering

All lookup-file I/O must finish before Tone opens.

Target order:

```text
load settings
load qsocalls.csv
finish Filesystem startup reads
open Tone
initialize Keyer
attach bounded table
initialize UI
```

Equivalent sequencing is acceptable if no callsign-table Filesystem I/O occurs
after Tone open.

No runtime reload is required in T046. Editing `qsocalls.csv` takes effect on
the next `minicw` launch.

## Keyer table ownership

Reintroduce a no-heap table attachment seam in `keyer_service`.

Preferred shape:

```c
void keyer_service_set_op_table(const keyer_op_entry_t *entries, size_t count);
```

The service must **not** free or allocate the table.

The caller guarantees the static table lifetime for the entire Keyer session.

Do not change candidate parsing / slash handling / 72/73 logic except where
required to reconnect the table.

## UI

T045's fixed top line is immutable:

```text
HH:MM KIN KOUT WW Vnn
```

Callsign lookup must never replace or modify that header.

Use row 6 / the existing cyan lower status line.

When:

- Tune is inactive;
- no explicit transient status is active;
- TX-tail text is empty;
- `keyer_service_get_op_name()` is nonempty;

show:

```text
OP:<name>
```

Example:

```text
OP:Alice
```

Priority remains:

```text
Tune
> explicit status
> TX tail
> OP name
> blank
```

Do not add another screen or menu.

The existing recognizer clears the operator name on 72/73 and Tune/reset paths;
preserve that behavior.

## Persistence / Filesystem interaction

T044A settings persistence remains unchanged.

Do not:

- combine `qsocalls.csv` with `setting.txt`;
- rewrite `qsocalls.csv`;
- add runtime file writes;
- change the persistence quiet-save state machine.

T046 is read-only with respect to the callsign table.

## Tests

### CSV parser / loader

Cover:

- missing file -> zero entries, no write;
- header;
- blank/comment rows;
- valid rows;
- lowercase call normalized uppercase;
- surrounding whitespace;
- malformed/no-comma rows skipped;
- extra-comma name row skipped;
- invalid call characters skipped;
- overlong call/name skipped;
- capacity truncation;
- read/open/close failures are nonfatal;
- oversized input is nonfatal;
- no heap calls.

### Lookup domain

Preserve and prove:

- exact base-call match;
- portable/slash call base extraction;
- own call excluded;
- no-digit candidate ignored;
- first duplicate wins;
- 72 clears;
- 73 clears;
- unknown call leaves no false name.

### UI

Prove:

- fixed T045 header never changes;
- TX tail has priority over OP name;
- transient status has priority over OP name;
- Tune has priority over OP name;
- OP name appears on lower line when otherwise idle;
- cleared lookup removes OP line.

### Startup / boundary

Prove:

- CSV read completes before Tone open;
- no callsign Filesystem I/O after Tone open;
- external ELF still imports only `mini_api_get`;
- dependency/platform/no-heap checks pass.

### Full gates

Run:

- full Linux CTest;
- portable units;
- focused Mini-CW tests;
- architecture/boundary checks;
- real ADV firmware build;
- clean external ELF build and inspect;
- `git diff --check`.

Report exact external BSS/ELF delta.

## Hardware acceptance

Install only the new T046 `minicw.elf` over the audited resident firmware.

Validate in this order:

1. paddle clean / no pop;
2. M1 clean / no pop;
3. persistence still works;
4. place a small `/flash/minicw/qsocalls.csv` with a known test call;
5. relaunch `minicw`;
6. enter/send the test callsign;
7. confirm `OP:<name>` appears on the lower line without changing the header;
8. confirm unknown call does not produce a name;
9. confirm 72/73 clears it;
10. Ctrl+C returns silent.

Any audio regression is immediate failure.

Recovery:

```text
golden/minicw-audited-baseline
00d540baef94c2f5b36511818c9d3f728a909846
```

## Non-goals

Do not:

- add online lookup;
- add QRZ/HamQTH/network access;
- add GPS;
- add logging;
- add heap;
- add runtime CSV reload;
- modify the fixed header;
- modify audio;
- modify resident MiniShell.

## Branch

Use:

```text
codex/T046-minicw-callsign-lookup
```

No PR and no hardware testing by Codex.

Set T046 to REVIEW and return exact SHA plus tests/resource/audio guard evidence.


## Implementation handoff

Task branch baseline: `fb2fb0059dddeff6ef737aa3bd2ebab9864346c6`.
Recovery: `00d540baef94c2f5b36511818c9d3f728a909846`.
Branch: `codex/T046-minicw-callsign-lookup`. Commit reference: the single
implementation commit containing this handoff; exact SHA returned after push.

### Implementation summary / files changed

- `apps/minicw/src/storage_service/storage_service.{c,h}` adds the bounded CSV
  loader/parser and the 192-entry limit. The existing private MiniShell text-read
  wrapper enforces a 4,095-byte payload limit and closes before parsing/returning.
  CSV parsing uses a 128-byte line buffer; overlong/malformed rows are skipped.
  Names retain case, calls normalize to uppercase, and first duplicate wins.
  Optional headers/comments/blank lines and CRLF are supported. Extra commas,
  non-ASCII/nonprintable names and invalid call characters are rejected per row.
  Oversized/NUL-containing/unreadable files yield an empty, nonfatal result.
- `apps/minicw/src/app_core/app_core.c` owns the static 192-entry table and performs
  settings load, CSV load, existing Tone open, Keyer initialization, borrowed-table
  attachment and UI initialization in that order. It surfaces `Lookup truncated`
  or `Lookup unavailable`; settings errors retain priority if both occur. Missing
  CSV files remain silent. No callsign I/O is performed after startup and no
  callsign writes are added. Settings persistence/quiet-save functions have no
  changes. The settings and CSV 4 KiB read buffers have sequential lifetimes.
- `apps/minicw/src/keyer_service/keyer_service.{c,h}` changes the table pointer to
  const and adds a borrowed-table attachment setter, which initializes name/
  recognition state. No allocation/free or ownership transfer. Existing
  candidate, slash/base-call, own-call, duplicate lookup and 72/73 routines are
  unchanged. As in the pinned recognizer, the last match persists until an
  existing clear/reset event; unmatched text does not assign a new name.
- `apps/minicw/src/ui_service/ui_service.c` adds only the normal lower-line OP
  fallback and an idle transient-status expiry refresh so OP can become visible
  after a status expires without waiting for another key or UTC minute. Priority
  is Tune > transient status > TX tail > OP > blank. Cyan lower-line styling and
  the fixed T045 header are unchanged. No screen/menu or timer/task is added.
- `tests/minicw_fs_fake.h` adds a separate read-only CSV fixture; existing settings
  and fault-injection behavior remains. `tests/minicw_lookup_test.c` and
  `tests/minicw_tests.cmake` add parser/load, domain, display-priority and startup
  ordering regressions. Existing Mini-CW/Tone/persistence tests remain passing.
  `apps/minicw/README.md` documents lookup behavior and limits.

### External packaging adaptation

The initial clean build and a link-only retry reproducibly failed in Xtensa
GCC 14.2.0's linker with `double free or corruption (out)` while linking with
`--strip-all --strip-debug --strip-discarded`. The partial artifact was not
accepted. Removing only those link-time strip options, then running the existing
post-link `elf-strip` command, succeeded and passed the unchanged ELF inspector.

`platform/adv/elf_apps/minicw/CMakeLists.txt` now makes that narrowly scoped
adaptation to a **build-local** copy of the pinned `elf_loader.cmake` module.
It requires the exact upstream strip-options block, failing configuration if
that block changes, and removes it from the local copy. The final existing
`--strip-unneeded`/section-removal command remains unchanged. Managed dependency
sources, resident build settings, compiler optimization/relaxation, linker
geometry and Mini-CW runtime code are not altered by this adaptation. A fresh
fullclean/build then passed all 1,074 build steps. This external packaging change
was necessary to deliver the required clean ELF; no resident/audio change or
architecture expansion was used.

### Behavior / invariants preserved

All eight protected files match the audited recovery commit byte-for-byte.
The fixed `ui_service_keyer_header()` implementation also matches byte-for-byte.
No public API, resident production code, audio wrapper, Tone worker, DMA/task/
queue/profile, Morse timing, KeyOut, existing `keyer` or FT8 change. No heap,
network, runtime reload, callsign-file mutation or new persisted setting. Table
storage belongs exclusively to the external app for its full session lifetime.

### Tests run and results

```sh
cmake -S . -B build-linux
cmake --build build-linux -j8
ctest --test-dir build-linux --output-on-failure
# PASS: 85/85
cmake -S tests/unit -B /tmp/T046-unit
cmake --build /tmp/T046-unit -j8
ctest --test-dir /tmp/T046-unit --output-on-failure
# PASS: 27/27
ctest --test-dir build-linux -R minicw --output-on-failure
# PASS: 7/7
ctest --test-dir build-linux -R 'minicw|tone|architecture|boundary' --output-on-failure
# PASS: 20/20, including dependency/platform/no-heap checks

source /home/wei/projects/esp-idf/export.sh
idf.py -C platform/adv build
# PASS: real resident build before/after changes
idf.py -C platform/adv/elf_apps/minicw fullclean
idf.py -C platform/adv/elf_apps/minicw elf
# PASS: final clean build after the packaging adaptation above
python3 tests/minicw_elf_inspect.py platform/adv/elf_apps/minicw/build/minicw.app.elf
# PASS: sole resident import mini_api_get; 634 mapped relocations; packed alignment valid
xtensa-esp32s3-elf-size -A platform/adv/build/minishell_adv.elf
xtensa-esp32s3-elf-size -A platform/adv/elf_apps/minicw/build/minicw.app.elf
cmp /tmp/T046-before.bin platform/adv/build/minishell_adv.bin
# PASS: identical
git diff --check
# PASS
```

New tests cover missing files/no writes, headers/comments/blank rows, CRLF,
normalized/trimmed fields, invalid/extra commas, invalid and overlong fields,
long-row skipping, first 192 entries retained, short reads, open/read/partial-read/
close failures, oversized input, exact and slash-call matching, own-call/no-digit/
unknown candidates, duplicate precedence, 72/73 clear, Tune reset, lower-line
priority and idle status expiry without header changes. Startup observers prove
CSV close/read completion before Tone open, no subsequent CSV reads/opens, and
repeated clean launches. Nonfatal loader errors/truncation retain normal startup.

### Explicit binary / audio / resource evidence

All protected paths were compared to
`git show 00d540baef94c2f5b36511818c9d3f728a909846:<path>`:

```text
protected audio diff: NONE
fixed header code:    IDENTICAL
resident BIN:         IDENTICAL (before/after real build)
resident SRAM delta:  0 bytes
resident imports:     mini_api_get only
```

The task-baseline resident inputs match the audited recovery checkpoint. Both
local resident BINs are 1,381,280 bytes, SHA-256:

```text
b11fc8c6c2af06f6efd02ec8784208e97db0177ff659aeedaa514b685a017416
```

ESP-IDF v5.5.4 / Xtensa GCC 14.2.0:

| Resident section | Before | After | Delta |
| --- | ---: | ---: | ---: |
| `.iram0.text` | 63,959 | 63,959 | 0 |
| `.dram0.data` | 27,016 | 27,016 | 0 |
| `.dram0.bss` | 40,336 | 40,336 | 0 |
| Static internal SRAM delta | — | — | **0** |

| External `minicw.app.elf` | Before | After | Delta |
| --- | ---: | ---: | ---: |
| File bytes | 43,024 | 44,128 | +1,104 |
| `.text` | 29,648 | 30,508 | +860 |
| `.rodata` | 1,940 | 2,032 | +92 |
| `.data` | 1,284 | 1,284 | 0 |
| `.bss` | 3,276 | 6,924 | **+3,648** |
| Section-loader text + data allocation | 36,148 | 40,748 | +4,600 |

The table is exactly 192 × 19 = **3,648 bytes**, entirely in external BSS.
Other final ELF sections: `.hash` 40, `.dynsym` 80, `.dynstr` 38, `.rela.dyn`
7,632, `.rela.plt` 12, `.eh_frame` 92, `.got` 4; `size -A` total 48,646. File-size
and loaded-size totals differ due to ELF packing/BSS. No heap allocation or new
stack/task is added. Sequential startup reads use one 4,096-byte buffer at a
time; the CSV parser's 128-byte line buffer is smaller than the settings parser's
160-byte line buffer. Hardware stack/free-heap readings were not taken.

### Hardware/manual validation still required / known risks

No PR or hardware testing was performed. After supervisor review, install only
the new external ELF over the audited resident firmware and follow the packet's
ordered acceptance: paddle/M1 audio first, then persistence, CSV/OP display and
clearing, unchanged header and silent exit. Software tests and binary guards do
not establish acoustic acceptance. Any audible regression requires stopping and
reverting to `golden/minicw-audited-baseline` at
`00d540baef94c2f5b36511818c9d3f728a909846`; no audio tuning belongs in T046.
The file and table limits are intentional; edits require a new app launch.

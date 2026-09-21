# T046 — Mini-CW callsign -> operator-name lookup

Status: READY

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

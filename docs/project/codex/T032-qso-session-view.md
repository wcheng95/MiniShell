# T032 — V -> 3 daily QSO compact view

Status: READY

## Architect intent

Implement the next MiniFT8-V3 read-only V-screen feature:

```text
V -> 3 QSO / Log
```

It must show the QSOs for the **current UTC day** in a compact list derived
from the existing daily ADIF log, using the pinned MiniFT8-V2 QSO entry view as
the behavioral reference.

For T032, "current session (today)" means the current day's MiniFT8 QSO history,
not only QSO objects still in AutoSeq memory and not only QSOs made since the
current process was launched. If MiniFT8 is restarted during the same UTC day,
V -> 3 must still show earlier QSOs already present in that day's
`YYYYMMDD.txt`.

The compact list is the V2 default semantic view:

```text
HH:MM band call
```

adapted to the locked 20-column ADV presentation.

Example:

```text
03:04 20m W6ABC
05:17 40m K1ABC
11:42 17m W1AW/9
```

## Objective

Replace the current V -> 3 prototype page with a bounded, heap-free,
platform-independent, read-only paged view of today's existing MiniFT8 ADIF
records.

Use the existing ADIF file as the single source of truth. Do not create a second
QSO database or change the logging format.

## Current context

At task creation, current `main` is:

```text
1c24d641f508031b46d280002018528028921963
```

Current V root already contains:

```text
1 Memory >
2 GPS >
3 QSO / Log >
4 Performance >
5 System Info >
6 About >
```

but `UI_SUBMENU_V_QSO` is only a placeholder:

```text
QSOs: 0 (prototype)
Last QSO: --
ADIF: --
RxTx Log: --
```

Current accepted daily ADIF path is:

```text
/flash/ft8/YYYYMMDD.txt
```

and `log_service_write_adif()` already creates one append-ordered ADIF record
per completed eligible QSO using MiniShell UTC.

Current ADIF records contain at least:

```text
<call:N>...
<qso_date:8>YYYYMMDD
<time_on:6>HHMMSS
<freq:N>...
...
<eor>
```

Current MiniShell Filesystem already exposes portable open/read/seek/stat/close.
No public filesystem API work is required.

## Pinned V2 reference

Use exactly this repository/commit for behavior reference:

```text
wcheng95/Mini-FT8
491e757ae6b1e4cfd2b9a6ba10f48b35643849e0
```

Relevant V2 source:

```text
main/main.cpp
```

In particular inspect:

```text
QsoLogEntry
qso_trim_head()
qso_load_entries()
qso_rebuild_entry_lines()
qso_draw_page()
```

Important V2 facts:

- QSO entries are read from the daily ADIF file, not from active AutoSeq state.
- Six QSO records are shown per page.
- Records remain in file order: oldest to newest.
- The default compact entry view is `time / band / call`.
- V2 also has a file browser and an alternate R/SNR-S/SNR entry view.

T032 intentionally keeps only the compact daily entry list. Do **not** port the
V2 file browser or alternate SNR view.

## V3 UI decision

### Entry

From V top level:

```text
V -> 3
```

enters `UI_SUBMENU_V_QSO` and loads page 1 of today's UTC ADIF log.

This is a read-only view. Entering it must not modify log contents.

### Row format

ADV has exactly 20 columns. Each QSO row must fit without renderer overflow.

Use:

```text
HH:MM BBB CALL
```

where:

- `HH:MM` is ADIF `time_on` truncated to minute resolution, matching V2;
- `BBB` is the current canonical MiniFT8 band name such as `20m`, `40m`,
  `17m`;
- `CALL` is the logged DX callsign;
- normal short calls are shown unchanged;
- if needed to fit the 20-column row, truncate the callsign head-style using
  the V2 convention: preserve the beginning and use `>` as the final visible
  character.

With a three-character band, the row budget is:

```text
5 + 1 + 3 + 1 + 10 = 20
```

so the callsign display field is at most 10 characters in ADV compact view.

Examples:

```text
03:04 20m W6ABC
11:42 17m W1AW/9
14:55 40m VERYLONGC>
```

Do not add row numbers; V2's QSO entry view is a raw compact list.

Desktop may use the same compact semantic rows. T032 does not require a wider
desktop-specific QSO layout.

### Empty/error states

If today's ADIF file does not exist or contains zero valid QSO records:

```text
No QSOs
```

This is normal and must not be treated as an application error.

If UTC is unavailable:

```text
UTC unavailable
```

If the daily file exists but cannot be read due to a real filesystem error:

```text
QSO log read error
```

A QSO-view read error is diagnostic/read-only UI state. It must **not** terminate
MiniFT8, stop RX, clear AutoSeq, or affect TX.

Malformed/unrecognized individual ADIF records should be skipped safely rather
than crashing the application.

### Pagination

Six valid QSO records per page.

Preserve V2 ordering:

```text
oldest -> newest
```

The ADV top line page field must reflect this QSO view's real page count.

Examples:

```text
6 QSOs  -> 1/1
7 QSOs  -> 1/2, 2/2
13 QSOs -> 1/3, 2/3, 3/3
```

When there are zero QSOs, page count is still presented as `1/1`.

Inside `V -> 3`:

```text
Up / Page Up       previous QSO page, wrap
Down / Page Down   next QSO page, wrap
Back / `           return to V root
R/T/O/S/V          existing UIScreen switching behavior remains
```

Rows are not selectable. Numeric 1-6 and Enter have no QSO mutation meaning.

Left/Right do nothing in T032. The V2 alternate SNR display is explicitly
deferred.

## Data ownership and architecture

Preserve current boundaries.

### log_service

`log_service` owns the daily ADIF persistence format and therefore owns parsing
stored ADIF records back into small QSO summary facts.

Add a bounded read API equivalent to:

```text
read today's QSO page(page_index, capacity=6)
    -> status
    -> total valid QSO count / page count
    -> up to six summary facts
```

Exact names/types are implementation detail.

A summary fact should carry facts, not pre-rendered UI strings, for example:

```text
hour/minute or HHMM
band identity/index
DX callsign
```

The parser may rely on the V3-produced one-record-per-line ADIF format. It must
still be bounds-safe:

- no heap allocation;
- fixed line/read buffers;
- one malformed or oversized record cannot overflow memory;
- oversized/malformed records may be skipped;
- partial MiniShell FS reads must be handled;
- every opened handle receives exactly one close attempt.

Do not use `storage_service_read_text()` with a whole-day fixed buffer. A daily
log can grow beyond that buffer. Stream through MiniShell Filesystem instead.

### app_controller

`app_controller` remains the sole coordinator.

It owns the currently loaded QSO-view page snapshot and exposes those facts in
the complete `UiModel`.

It may add a read-only UI action equivalent to:

```text
APP_ACTION_LOAD_QSO_PAGE
```

for entering V -> 3 and changing pages.

The controller delegates disk parsing to `log_service`; it must not parse ADIF
tags itself.

QSO-view loading must not alter:

- AutoSeq queue/state;
- RX batch/display state;
- current band;
- CAT state;
- TX lifecycle;
- persisted configuration.

### ui_shell

`ui_shell` owns:

- entering/leaving the QSO submenu;
- page navigation;
- page number presentation;
- compact row rendering/truncation.

It must not call MiniShell Filesystem or `log_service` directly.

### Refresh behavior

Entering V -> 3 always reloads page 1 from today's file.

Changing a QSO page loads that requested page.

If a successful ADIF QSO is committed while a QSO view has already been loaded,
the controller must make the cached daily view refreshable without requiring
MiniFT8 restart. A small controller dirty flag plus a bounded controller
progression step is acceptable.

Minimum accepted live behavior:

- a newly logged QSO becomes visible in V -> 3 without restarting MiniFT8;
- leaving V -> 3 and re-entering must always refresh from disk.

Do not poll or rescan the file on every render/main-loop iteration.

If the UTC date rolls over while MiniFT8 remains running, a subsequent entry to
V -> 3 must use the new day's file.

## Memory constraints

T032 must remain ADV-safe:

- no heap allocation;
- do not cache the whole daily ADIF file;
- do not cache an arbitrary number of QSO records;
- cache only bounded page/view metadata and up to six QSO summary rows;
- fixed parser buffers belong in a clearly owned object or stack where measured
  stack limits remain safe.

If a new fixed controller/log-service field changes the ADV application
allocation, report the exact size delta.

## Non-goals

Do not include any of these in T032:

- V2 QSO file browser;
- historical-day selection;
- alternate SNR QSO page;
- ADIF editing/deleting;
- QSO row selection;
- Cabrillo browsing;
- RxTxLog browsing;
- new log format;
- changing ADIF records;
- changing logging eligibility;
- changing AutoSeq logging ACK rules;
- color coding;
- touch-specific behavior;
- QSO search/filter/sort controls;
- public MiniShell API changes;
- platform-specific filesystem calls.

The root label may remain `QSO / Log >` for now even though T032 implements
only today's QSO compact list.

## Suggested implementation shape

One reasonable bounded shape is:

```text
LogQsoSummary
    time HHMM
    band index
    call[bounded]

LogQsoPage
    status
    total_count
    page_index
    page_count
    row_count
    rows[6]
```

Flow:

```text
V root
  -> key 3
  -> ui_shell enters UI_SUBMENU_V_QSO
  -> emits load page 0 action
  -> app_controller
  -> log_service
  -> MiniShell Filesystem
  -> /flash/ft8/YYYYMMDD.txt
  -> bounded ADIF parse
  -> controller snapshot
  -> UiModel
  -> ui_shell compact render
```

Next/previous page repeats the bounded file scan for only the requested page.
The implementation may count valid records during that scan to determine total
pages.

This intentionally trades a small user-triggered sequential file read for
bounded RAM and simple ownership.

## Acceptance criteria

- [ ] V -> 3 no longer displays prototype text.
- [ ] V -> 3 directly opens today's UTC QSO list; no intermediate file browser.
- [ ] Existing QSOs from earlier MiniFT8 launches on the same UTC day are shown.
- [ ] Each compact ADV row is V2-style `HH:MM band call` and fits 20 columns.
- [ ] Calls too long for the row are safely truncated with trailing `>`.
- [ ] Six valid QSOs are shown per page.
- [ ] QSO order is oldest to newest, matching V2.
- [ ] ADV top-line page count reflects QSO pages.
- [ ] Up/Down and Page Up/Page Down wrap QSO pages.
- [ ] Back returns to V root.
- [ ] No daily file / zero valid records renders `No QSOs`.
- [ ] UTC-unavailable and read-error states are visible but non-fatal.
- [ ] Malformed/oversized ADIF records cannot overflow buffers and are skipped.
- [ ] Partial filesystem reads are handled.
- [ ] No entire daily file is loaded into a fixed whole-file buffer.
- [ ] No heap allocation is added.
- [ ] No AutoSeq/RX/TX/CAT/config state changes from viewing QSOs.
- [ ] Newly logged QSOs can be observed without restarting MiniFT8.
- [ ] Re-entering V -> 3 always reloads today's file.
- [ ] Existing ADIF write format is unchanged.
- [ ] No MiniShell public API/platform code changes.

## Automated tests

### log_service read tests

Extend `ft8_log_service_unit` or add a focused unit for daily QSO reading.

Cover at minimum:

1. no daily file -> empty/No-QSO status, not error;
2. one valid V3 ADIF record;
3. exact extraction of time, band and call;
4. 6 records -> one page;
5. 7 records -> two pages;
6. 13 records -> three pages;
7. page 0/page 1/page 2 return the correct file-order entries;
8. missing optional RST/grid/comment fields do not affect compact parsing;
9. malformed record skipped;
10. oversized line safely skipped;
11. partial reads across ADIF tag/value boundaries;
12. read/open/close failures produce read-error status and no leaked handle;
13. UTC unavailable does not guess a date/path;
14. current-day filename matches the same UTC date convention used by ADIF write.

### UI smoke tests

Extend `ft8_ui_smoke`:

- V -> 3 enters `UI_SUBMENU_V_QSO`;
- entry requests page 0 load;
- render zero state as `No QSOs`;
- render representative compact rows;
- 20-column ADV row limit is preserved;
- 7-entry page metadata renders top-line `1/2`, then `2/2`;
- Up/Down and Page Up/Page Down wrap;
- Back returns to V root;
- Left/Right/numeric/Enter do not mutate QSO state.

### Controller integration

Add focused controller coverage proving:

- load-page action delegates to log_service-backed daily data;
- loading does not modify AutoSeq bytes, RX facts, TX state, band or CAT state;
- a real successful ADIF logging event can later appear in the QSO view;
- re-entry reloads current file contents rather than relying on a stale process-only list.

### Full regressions

Run at minimum:

```bash
cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"

ctest --test-dir build-linux --output-on-failure -R 'ft8_(log_service|ui_smoke|qso|physical_tx)'

ctest --test-dir build-linux --output-on-failure
```

Also run the portable unit suite and architecture checks required by AGENTS.md.

Because this changes shared portable MiniFT8 data/UI structures, run a real ADV
build:

```bash
source ~/projects/esp-idf/export.sh
idf.py -C platform/adv build
```

Report any controller/UiModel fixed-size RAM increase.

## Manual / hardware validation

After supervisor review/merge, architect validates on normal MiniFT8/QMX
operation.

Recommended validation:

1. Ensure today's `/flash/ft8/YYYYMMDD.txt` has at least one real QSO.
2. Launch MiniFT8.
3. Enter V -> 3.
4. Confirm rows show compact time/band/call matching the existing ADIF log.
5. With more than six test/real records if available, verify page navigation.
6. Exit back to V root and re-enter; confirm the same current-day data reloads.
7. Complete another QSO, then verify the new entry becomes visible without
   restarting MiniFT8.
8. Confirm RX/TX behavior is otherwise unchanged.

Linux validation is sufficient for filesystem/UI semantics. ADV build is
required; ADV screen confirmation is desirable because 20-column formatting is
the primary presentation constraint.

## Codex implementation notes

Codex fills this section before handoff.

### Implementation summary

### Files changed

### Invariants preserved

### Local tests run

### Manual/hardware validation still required

### Known limitations / risks

### Commit

## Supervisor review

Supervisor reviews the actual `main..<commit>` diff against this task and
recorded test evidence. No unrelated cleanup.

Canonical MiniFT8 docs are updated after accepted implementation/hardware
behavior.

## Architect test result

Pending.

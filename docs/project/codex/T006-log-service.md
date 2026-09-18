# T006 — Extract MiniFT8 logging ownership

Status: REVIEW

## Architect intent

Resolve T001 finding F06 before further feature work grows around logging.

The current logging behavior is functionally correct, but too much representation/storage policy lives inside `app_controller_tx.c`. The controller should coordinate TX-start ordering and AutoSeq ACKs; a dedicated application-owned logging module should own ADIF/Cabrillo serialization and file mutation.

This task is **behavior-preserving extraction only**.

## Objective

Create one explicit MiniFT8 logging owner beneath `app_controller` so the dependency direction becomes:

```text
AutoSeq
    owns QSO state + typed log eligibility only
        |
        v
app_controller
    owns TX-start ordering and independent success ACKs
        |
        v
log_service
    owns ADIF/Cabrillo representation, filenames, UTC formatting,
    band-frequency mapping, and MiniShell Filesystem mutation
        |
        v
MiniShell Filesystem + Time/Location
```

The controller must no longer contain ADIF/Cabrillo formatting or raw log-file mutation code.

## Source finding

T001 F06:

> Logging combines coordination, serialization, and file policy.

Current `app_controller_tx.c` owns:

- civil date conversion;
- UTC extraction;
- FT8 band-frequency tables;
- daily ADIF filename/path construction;
- ADIF field serialization;
- Field Day Cabrillo header/QSO serialization;
- append/seek/sync/close mechanics;
- Cabrillo `END-OF-LOG:` placement;
- TX-start ordering and AutoSeq ACKs.

Only the final two bullets belong to the controller boundary: ordering and ACK decisions.

## Source of truth

Read before editing:

```text
AGENTS.md
docs/project/codex/T001-architecture-boundary-audit.md
docs/MiniFT8/README.md
docs/MiniFT8/development.md
docs/MiniFT8/architecture.md

apps/ft8/src/app_controller/app_controller_internal.h
apps/ft8/src/app_controller/app_controller_tx.c
apps/ft8/src/auto_seq/auto_seq.h
apps/ft8/src/storage_service/storage_service.h
apps/ft8/src/config_service/config_service.h
CMakeLists.txt
```

For preserved output behavior, use the current V3 implementation as the immediate extraction reference and the pinned V2 behavioral reference when needed:

```text
wcheng95/Mini-FT8
491e757ae6b1e4cfd2b9a6ba10f48b35643849e0
```

Do not copy V2 platform structure.

## Architectural constraints

### Controller remains coordinator

`app_controller` continues to own:

- observing the TX slot boundary;
- preparing AutoSeq TxIntent;
- preparing `AutoSeqLogEvent`;
- deciding whether ADIF and/or Cabrillo are eligible;
- invoking the logging owner;
- passing each write result independently to `auto_seq_ack_log_event()`;
- ticking AutoSeq after the TX-start/logging boundary.

Do not move QSO state, log eligibility, retry state, or ACK state into the logging module.

### Logging owner

Create one small application-private module, preferred path:

```text
apps/ft8/src/log_service/
    log_service.h
    log_service.c
```

Equivalent naming is acceptable if clearly application-owned.

The module may use the generic MiniShell Filesystem and Time/Location APIs. It must not use POSIX, libc time conversion, Linux/ALSA, ESP-IDF, or platform-private interfaces.

The module owns:

- daily ADIF filename;
- `fieldday.txt` filename;
- path construction beneath the FT8 data directory;
- UTC calendar/time formatting;
- band-index -> dial-frequency representation;
- ADIF serialization;
- Cabrillo header serialization;
- Field Day QSO line serialization;
- current append/seek/sync/close mechanics.

### Immutable facts in

Do not make `log_service` reach sideways into `AppController`, `AutoSeq`, or UI state.

Pass explicit immutable facts needed for one write, for example:

```text
station callsign
effective runtime grid
band index
local Field Day exchange
AutoSeqLogEvent
```

A small `LogStationFacts`/equivalent private struct is preferred over passing `ConfigService *` or `AppController *`.

`LogService` may retain only stable generic service pointers and the FT8 data directory/path prefix established at initialization.

### Runtime grid ownership

T004 is now part of the baseline:

- `config.grid` is persistent station configuration;
- `effective_grid` is controller-owned runtime location;
- ADIF must continue to log the **effective runtime grid**.

Do not regress this boundary.

### Persistence semantics

Do **not** fix T001 F07 in this task.

Preserve current observable success/failure behavior, including current Cabrillo create/append mechanics. T007 will separately define recovery/partial-write semantics.

Do not combine extraction with transactional redesign.

### Public API and format

No public MiniShell API changes.

No station/config format changes.

No ADIF/Cabrillo format changes.

No filename changes.

## Current output contracts to preserve

### ADIF

Daily path:

```text
/flash/ft8/YYYYMMDD.txt
```

Preserve exact current behavior:

- FT8 mode;
- UTC date/time;
- standard selected dial frequency mapping;
- station callsign;
- station grid truncated to first four chars;
- **effective runtime grid**, not persisted grid when live location is active;
- unknown `AUTO_SEQ_SNR_UNKNOWN (-99)` reports omitted;
- empty comment field remains `<comment:0> `;
- newline and field ordering unchanged;
- success reported only after current write/sync/close path succeeds.

### Field Day Cabrillo

Path:

```text
/flash/ft8/fieldday.txt
```

Preserve:

- existing V2-style header;
- current local-section extraction;
- optional leading `R ` stripping;
- current kHz frequency mapping;
- `QSO: ...` exact layout;
- insertion immediately before `END-OF-LOG:`;
- current result semantics.

## Implementation scope

Expected production files:

```text
apps/ft8/src/log_service/log_service.h
apps/ft8/src/log_service/log_service.c
apps/ft8/src/app_controller/app_controller_internal.h
apps/ft8/src/app_controller/app_controller.c       # only if initialization is needed
apps/ft8/src/app_controller/app_controller_tx.c
CMakeLists.txt
```

A `LogService` field in `AppController` is appropriate if the module is initialized once with generic MiniShell services/data path.

Expected tests:

```text
tests/ft8_log_service_test.c
```

and existing:

```text
tests/ft8_tx_lifecycle_as7_test.c
```

Update task notes before handoff.

## Required tests

### Focused log-service test

Use fake MiniShell Time/Location and Filesystem APIs.

Prove exact current serialization/storage behavior for at least:

1. ADIF with known TX/RX reports;
2. ADIF with unknown `-99` fields omitted;
3. ADIF using an explicit effective grid different from persistent/manual grid;
4. correct daily filename from UTC;
5. correct frequency for at least 20m and one other band;
6. Field Day new-file header + one QSO;
7. Field Day append before existing `END-OF-LOG:`;
8. independent ADIF/Cabrillo success return values.

Compare exact emitted text, not only substring presence.

Do not add F07 fault-recovery redesign tests here beyond what is necessary to preserve current boolean result behavior.

### Controller boundary regression

Keep/extend `ft8_tx_lifecycle_as7_test.c` so it demonstrates:

- controller still asks AutoSeq for log eligibility at the same TX-start point;
- ADIF/Cabrillo are acknowledged independently;
- AutoSeq receives no Filesystem or Time/Location dependency;
- the logging module is invoked through the controller rather than AutoSeq.

Do not weaken current TX-start/log-event assertions.

### Architecture enforcement

Update dependency rules if needed so:

```text
app_controller -> log_service
log_service -> MiniShell public API
log_service -X-> AutoSeq internals/AppController/UI/platform
```

is enforceable.

## Non-goals

Do **not**:

- fix F07 partial-write/retry/recovery semantics;
- add RTYYMMDD trace logging;
- add comment/radio macros;
- change log filenames or formats;
- move eligibility/ACK state out of AutoSeq;
- move TX-start ordering out of app_controller;
- implement physical TX;
- change public MiniShell APIs;
- alter T004 runtime-grid semantics;
- fix the two known baseline test failures;
- perform general canonical-doc cleanup.

If extraction appears to require any of those, stop and report the architectural conflict.

## Local build/test gate

Run **locally**, not through GitHub Actions.

At minimum:

```bash
git status --short

cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"

ctest --test-dir build-linux -R 'ft8_log|ft8_tx_lifecycle_as7|ft8_runtime_grid' --output-on-failure

python3 tests/app_dependency_boundary.py . ft8
python3 tests/app_dependency_boundary.py . keyer
python3 tests/ft8_platform_boundary.py .

cmake -S tests/unit -B /tmp/T006-build-unit
cmake --build /tmp/T006-build-unit -j"$(nproc)"
ctest --test-dir /tmp/T006-build-unit --output-on-failure

git diff --check
```

Then run the normal full Linux CTest once:

```bash
ctest --test-dir build-linux --output-on-failure
```

The accepted baseline still has two known unrelated failures unless separately fixed:

```text
linux_audio
linux_ft8
```

Record the exact local result. Do not wait for GitHub Actions.

## Handoff protocol — no PR

Work on:

```text
codex/T006-log-service
```

Before handoff:

1. update this file to `Status: REVIEW`;
2. fill implementation/test notes;
3. ensure working tree is clean;
4. push the branch;
5. return the single commit SHA.

**Do not open a pull request.**

The supervisor will review:

```text
main..<commit>
```

If the diff is clean and required local tests passed, the supervisor will fast-forward `main` directly.

## Acceptance criteria

- [x] Dedicated application-private logging owner exists.
- [x] app_controller no longer serializes ADIF/Cabrillo or mutates log files directly.
- [x] controller still owns TX-start ordering and independent ACK decisions.
- [x] AutoSeq remains pure and owns only eligibility/ACK state.
- [x] log_service receives immutable station/event facts rather than AppController/ConfigService ownership.
- [x] MiniShell FS/Time APIs are the only platform-visible dependencies.
- [x] exact ADIF output is preserved.
- [x] exact Field Day Cabrillo output is preserved.
- [x] T004 effective-grid logging behavior is preserved.
- [x] no F07 persistence redesign is mixed in.
- [x] focused exact-output tests added.
- [x] current TX lifecycle tests pass.
- [x] architecture checks pass.
- [x] local full build/test results recorded.
- [x] no unrelated cleanup.

## Codex implementation notes

Codex fills this section and changes `Status` from `READY` to `REVIEW`.

### Implementation summary

Extracted the existing ADIF/Cabrillo implementation into application-private
`log_service`. Controller TX-start ordering and independent AutoSeq ACKs remain
in place. No format changes, persistence redesign, or task deviations.

### Module ownership / dependency design

`LogService` retains only generic Filesystem/Time-Location pointers and a copied
path prefix, initialized from the station file's parent to preserve existing
path behavior. It owns UTC formatting, band frequencies, filenames, serialization,
and file mutation. Missing Time/Location remains a write failure rather than a
new application-initialization failure.

The controller passes borrowed, immutable `LogStationFacts` and `LogQsoFacts` for
each eligible write. Facts include the effective runtime grid and explicit
report-known flags translated from AutoSeq's unknown sentinel. The module has
no AutoSeq, ConfigService, AppController, or UI dependency, and retains no QSO
facts or ACK state. Dependency checkers allow controller -> log_service and
log_service -> public MiniShell API only (apart from standard C headers).

### Files changed

- `apps/ft8/src/log_service/log_service.h` and `.c`: private logging owner.
- `apps/ft8/src/app_controller/app_controller_internal.h`: LogService member.
- `apps/ft8/src/app_controller/app_controller.c`: initialize stable logging context.
- `apps/ft8/src/app_controller/app_controller_tx.c`: pass facts to logging owner;
  retain eligibility, ordering, and independent ACKs.
- `CMakeLists.txt`: production module, focused test, dependent include paths.
- `tests/ft8_log_service_test.c`: fake public services and exact-output assertions.
- `tests/ft8_tx_lifecycle_as7_test.c`: observable logging peer and ACK regressions.
- `tests/app_dependency_boundary.py`, `tests/ft8_platform_boundary.py`: enforce
  the new module's dependency boundaries.
- This packet: implementation notes and local evidence.

### Invariants preserved

All ten extracted private conversion/frequency/Filesystem/Cabrillo helpers were
compared with the baseline and are byte-for-byte unchanged. Output field order,
spacing/newlines, filenames, report omission, grid truncation, and FD exchange
normalization are preserved. Controller captures eligibility before logging,
ACKs each result independently, and ticks AutoSeq afterward. No heap allocation,
public API, station format, physical TX, or runtime-grid ownership changes.
F07 create/append and partial-write/retry semantics are intentionally unchanged.

### Tests added

`ft8_log_service_unit` checks exact ADIF output with known reports and omitted
unknown reports, explicit effective-grid truncation, UTC daily filename/time,
20m/40m frequency mappings, exact new Field Day header/QSO, and insertion of a
second QSO before the existing end marker. Fake FS verifies flags, partial-write
loops, seeks, sync/close counts, and independent ADIF/Cabrillo return values.
Basic sync/close failures preserve existing boolean outcomes without imposing
new recovery semantics.

Extended `ft8_tx_lifecycle_as7_unit` covers all four independent write-result
combinations. The logging peer observes pre-tick QSO state, captured eligibility,
station facts (manual CM87 versus effective CM97), and report-known translation.
Assertions verify ACK flags, post-write tick, and no duplicate-slot logging.
Existing TX-start assertions remain unchanged. AutoSeq links no filesystem or
time dependency; logging is invoked solely by the controller.

### Local tests run and results

Base: `df965ae2e2b99bfd0bb2c2f9577dd160b6959ed9`.

- `git status --short`: clean before starting.
- `cmake -S . -B build-linux`: PASS.
- `cmake --build build-linux -j"$(nproc)"`: PASS; repeated after final test
  assertions/header contract clarification, also PASS.
- `ctest --test-dir build-linux -R 'ft8_log|ft8_tx_lifecycle_as7|ft8_runtime_grid' --output-on-failure`:
  PASS, 3/3; final targeted rerun also 3/3.
- `python3 tests/app_dependency_boundary.py . ft8`: PASS.
- `python3 tests/app_dependency_boundary.py . keyer`: PASS.
- `python3 tests/ft8_platform_boundary.py .`: PASS (54 source/header files).
- `python3 tests/app_dependency_boundary.py --self-test`: PASS.
- `cmake -S tests/unit -B /tmp/T006-build-unit`: PASS.
- `cmake --build /tmp/T006-build-unit -j"$(nproc)"`: PASS.
- `ctest --test-dir /tmp/T006-build-unit --output-on-failure`: PASS, 14/14.
- `ctest --test-dir build-linux --output-on-failure`: 22/24 PASS. Only the
  documented baseline failures remain: `linux_audio` expects the old contiguous
  frames/hash output; `linux_ft8` expects the stale N5CH queue position. Neither
  test nor the associated product behavior was changed.
- `git diff --check`: PASS.

All evidence is local; no GitHub Actions wait or PR is part of this handoff.

### Hardware/manual validation still required

None expected for this extraction. Supervisor diff review and architect acceptance
remain outside this implementation handoff.

### Known limitations / risks

Existing F07 persistence failure/retry limitations remain intentionally deferred
to T007. The two known baseline tests still fail. Canonical-document reconciliation
remains with the supervisor after acceptance; no general documentation cleanup
was included.

### Commit

One implementation commit on `codex/T006-log-service`, containing this report.
The pushed commit SHA is returned in the engineer's handoff; the supervisor
reviews `main..<commit>`. No PR opened.

## Supervisor review

Supervisor reviews the actual diff against current `main` and the local test evidence.

## Architect test result

No hardware validation is expected for this behavior-preserving extraction unless the implementation introduces an unexpected hardware dependency.

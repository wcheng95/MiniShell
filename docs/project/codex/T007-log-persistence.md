# T007 — Harden MiniFT8 log persistence

Status: REVIEW

## Objective

Resolve T001 F07 now that T006 owns logging in `log_service`.

Change ADIF/Cabrillo persistence from in-place mutation to copy-on-write:

```text
read/validate committed final
        -> write complete <final>.tmp
        -> sync + close temp
        -> rename temp -> final
        -> success
```

The final log path must remain byte-for-byte unchanged until `rename()` succeeds.

## Ownership

Keep the T006 split unchanged:

```text
AutoSeq         eligibility + per-format ACK state
app_controller  TX-start ordering + invokes log_service + ACK decisions
log_service     serialization + persistence mechanics
MiniShell FS    namespace/open/read/write/sync/rename/remove
```

Do not move retry state into AutoSeq/controller. Do not change public MiniShell APIs.

## Explicit retry semantics

T007 guarantees:

- any known pre-commit failure leaves the committed final file unchanged;
- retry after such a failure is safe;
- one later successful retry adds exactly one record relative to the previously committed file;
- `rename(tmp, final) == MINI_OK` is the commit point; only then may log_service return true.

T007 does NOT claim crash-exactly-once across:

```text
rename succeeded
power/process loss
AutoSeq ACK was not persisted
```

Do not invent heuristic de-duplication from callsign/time/report fields. Legitimate repeat QSOs can share those facts. Crash-level exactly-once would require a separate persistent QSO identity design.

## ADIF design

Final path stays `/flash/ft8/YYYYMMDD.txt`.

1. `stat(final)`.
2. `MINI_OK`: existing file must be a regular file; stream-copy it to temp.
3. `MINI_ERR_NOT_FOUND`: start with empty temp.
4. any other stat result: fail without touching final.
5. open `<final>.tmp` with WRITE|CREATE|TRUNC.
6. copy existing bytes when present.
7. append the exact current ADIF record.
8. sync temp, close temp.
9. rename temp -> final.
10. return true only after rename succeeds.

Any source open/read/close, temp write/sync/close, or rename failure must preserve final.

## Cabrillo design

Final path stays `/flash/ft8/fieldday.txt`.

For an existing file:

1. only `MINI_ERR_NOT_FOUND` means new log; any other stat error fails;
2. validate exact trailing marker `END-OF-LOG:\n`;
3. stream-copy bytes before that marker to temp;
4. append exact new `QSO:` line + newline;
5. append exact `END-OF-LOG:\n`;
6. sync + close temp;
7. rename temp -> final.

For a new file, temp contains the existing exact header, then QSO, then END marker, followed by sync/close/rename.

Malformed existing Cabrillo is an error and must remain unchanged.

## Implementation constraints

- expected production change: `apps/ft8/src/log_service/log_service.c` only;
- `log_service.h` only if a private helper contract truly requires it;
- controller and AutoSeq production files should not change;
- fixed stack buffers + streaming copy; no heap;
- exact ADIF/Cabrillo filenames, text, spacing, field order, and effective-grid behavior from T006 stay unchanged;
- do not fix unrelated F08+ findings;
- do not fix `linux_audio` or `linux_ft8` baseline tests.

## Required fault-injection tests

Extend `tests/ft8_log_service_test.c`.

ADIF must prove:

- exact new-file commit;
- existing-file copy + one exact new record;
- stat I/O error preserves final;
- source open/read/close error preserves final;
- temp open/write/sync/close error preserves final;
- rename failure preserves final;
- failed attempt followed by retry produces exactly one new record;
- stale temp content is truncated/reused safely.

Cabrillo must prove:

- exact new header + QSO + END commit;
- exact append before existing END marker;
- stat I/O error preserves final;
- malformed/missing END preserves final;
- source open/read/close error preserves final;
- temp open/write/sync/close error preserves final;
- rename failure preserves final;
- failed attempt followed by retry produces exactly one added QSO.

Also assert that log_service never opens the final path for WRITE/TRUNC/APPEND before commit; only the temp path is writable.

## Local test gate

Run locally:

```bash
git status --short
cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"
ctest --test-dir build-linux -R 'ft8_log_service|ft8_tx_lifecycle_as7|ft8_runtime_grid' --output-on-failure
python3 tests/app_dependency_boundary.py . ft8
python3 tests/app_dependency_boundary.py . keyer
python3 tests/ft8_platform_boundary.py .
cmake -S tests/unit -B /tmp/T007-build-unit
cmake --build /tmp/T007-build-unit -j"$(nproc)"
ctest --test-dir /tmp/T007-build-unit --output-on-failure
git diff --check
ctest --test-dir build-linux --output-on-failure
```

Record exact local results. Do not wait for GitHub Actions.

## Branch workflow

Use the existing branch:

```text
codex/T007-log-persistence
```

Before handoff:

1. change Status to REVIEW;
2. fill implementation/test notes below;
3. commit once and push;
4. return the commit SHA;
5. do not open a PR.

After supervisor review and merge/fast-forward to main, delete the local and remote T007 branch.

## Acceptance criteria

- [x] final ADIF is untouched before rename commit;
- [x] final Cabrillo is untouched before rename commit;
- [x] only NOT_FOUND creates a new log;
- [x] stat/read/open/write/sync/close failures preserve committed bytes;
- [x] malformed Cabrillo preserves committed bytes;
- [x] retry after known failure is safe;
- [x] exact ADIF/Cabrillo output preserved;
- [x] controller/AutoSeq ownership unchanged;
- [x] no public API change and no heap;
- [x] focused fault-injection tests pass;
- [x] architecture checks pass;
- [x] local full-suite result recorded;
- [x] no unrelated cleanup.

## Codex implementation notes

### Implementation summary

ADIF and Cabrillo now build complete replacements in `<final>.tmp` and return
success only after rename commits the replacement. Production changes are
confined to `log_service.c`; controller, AutoSeq, public API, and the logging
interface are unchanged. No task deviations.

### Persistence / commit-point design

A shared private helper stats final and permits creation only on explicit
`MINI_ERR_NOT_FOUND`. Existing paths must be regular files. ADIF copies the
existing bytes; Cabrillo first validates the exact trailing END marker and
copies only the preceding bytes. Empty existing Cabrillo is malformed.

The helper opens final read-only and temp with WRITE|CREATE|TRUNC, streaming
through a fixed 512-byte stack buffer with complete-read/write loops. It appends
the serialized record and optional END marker, closes the source, syncs and
closes temp, then renames temp over final. Each acquired handle receives exactly
one close attempt; any earlier failure prevents rename. Failed attempts make a
best-effort removal of temp. Failed removal does not compromise final or retry:
the next attempt truncates/reuses temp. Rename returning MINI_OK is the sole
success/commit point.

### Files changed

- `apps/ft8/src/log_service/log_service.c`: shared copy-on-write mechanics and
  Cabrillo header serialization into the temporary replacement.
- `tests/ft8_log_service_test.c`: fake committed/temp files, fault injection,
  byte-preservation guards, cleanup and retry assertions.
- This task packet: REVIEW status and local implementation/test evidence.

### Invariants preserved

Exact filenames, ADIF fields/order/spacing/newlines, Cabrillo header/QSO/END
layout, frequency mapping, unknown-report omission, effective runtime grid, and
independent format results remain covered. AutoSeq eligibility/ACK state and
controller TX-start ordering are unchanged. No heap or platform-private APIs.
Final is never opened with write, truncate, or append flags.

The earlier tests' sync/close counts were updated narrowly: a new Cabrillo file
now commits one complete temporary file, replacing the old header-then-in-place
QSO sequence. Exact output assertions remain intact.

### Fault-injection tests added

For both formats, every filesystem operation in successful new/existing-file
traces is failed individually, followed by a successful retry: 84 total points
(ADIF new 15/existing 22; Cabrillo new 27/existing 20). This covers stat, source
open/read/close, temporary open/write/sync/close, marker seeks, and rename,
including failures after partial progress. Tests assert zero commits on failure,
unchanged final bytes/existence throughout operations, closed handles, and
exactly one committed record after retry. Cleanup removal deliberately fails
in these cases to prove safe reuse of stale temp content.

Additional tests cover exact ADIF creation and append, exact Cabrillo creation
and append, partial reads/writes, zero-progress reads/writes, directory targets,
multiple malformed/missing END cases, and streaming 1,600-byte prefixes including
embedded NUL bytes. Rename checks require synced temp and all handles closed.
The existing serialization and independent-format-result assertions remain.

### Local tests run and results

Base: `5ec7fa4dc468f1934f2ead6827ba771608ad3708`.

- `git status --short`: clean before implementation on the existing T007 branch.
- `cmake -S . -B build-linux`: PASS.
- `cmake --build build-linux -j"$(nproc)"`: PASS; final test-only rebuild PASS.
- `ctest --test-dir build-linux -R 'ft8_log_service|ft8_tx_lifecycle_as7|ft8_runtime_grid' --output-on-failure`:
  PASS, 3/3; final rerun after adding exact ADIF append assertions also PASS.
- `build-linux/ft8_log_service_unit`: PASS, including all 84 failure/retry points.
- `python3 tests/app_dependency_boundary.py . ft8`: PASS.
- `python3 tests/app_dependency_boundary.py . keyer`: PASS.
- `python3 tests/ft8_platform_boundary.py .`: PASS (54 source/header files).
- `cmake -S tests/unit -B /tmp/T007-build-unit`: PASS.
- `cmake --build /tmp/T007-build-unit -j"$(nproc)"`: PASS.
- `ctest --test-dir /tmp/T007-build-unit --output-on-failure`: PASS, 14/14.
- `git diff --check`: PASS.
- `ctest --test-dir build-linux --output-on-failure`: 22/24 PASS. Only the
  documented `linux_audio` output-substring and `linux_ft8` queue-order baseline
  failures remain; those tests and associated behavior were not changed.

All checks ran locally. No PR or GitHub Actions wait.

### Hardware/manual validation still required

None expected. Supervisor review and architect acceptance remain.

### Known limitations / residual crash window

Successful rename followed by process/power loss before AutoSeq ACK is not
crash-exactly-once. No heuristic deduplication or persistent QSO identity was
introduced. Filesystem/backend crash durability remains its own contract.
Copy-on-write requires space for a complete temporary replacement and copies
the existing log on each addition. Temporary cleanup is best effort; subsequent
attempts safely truncate leftovers. Concurrent external writers are not coordinated.

### Commit

One implementation commit on `codex/T007-log-persistence`, containing this
report. The pushed SHA is returned in the handoff. Branch deletion is deferred
until supervisor review and merge/fast-forward to main, as specified above.

## Supervisor review

Supervisor reviews the actual `main..<commit>` diff and local test evidence.

## Architect test result

No hardware validation is expected for this persistence change.
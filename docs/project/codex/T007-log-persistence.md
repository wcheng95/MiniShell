# T007 — Harden MiniFT8 log persistence

Status: READY

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

- [ ] final ADIF is untouched before rename commit;
- [ ] final Cabrillo is untouched before rename commit;
- [ ] only NOT_FOUND creates a new log;
- [ ] stat/read/open/write/sync/close failures preserve committed bytes;
- [ ] malformed Cabrillo preserves committed bytes;
- [ ] retry after known failure is safe;
- [ ] exact ADIF/Cabrillo output preserved;
- [ ] controller/AutoSeq ownership unchanged;
- [ ] no public API change and no heap;
- [ ] focused fault-injection tests pass;
- [ ] architecture checks pass;
- [ ] local full-suite result recorded;
- [ ] no unrelated cleanup.

## Codex implementation notes

### Implementation summary

### Persistence / commit-point design

### Files changed

### Invariants preserved

### Fault-injection tests added

### Local tests run and results

### Hardware/manual validation still required

### Known limitations / residual crash window

### Commit

## Supervisor review

Supervisor reviews the actual `main..<commit>` diff and local test evidence.

## Architect test result

No hardware validation is expected for this persistence change.
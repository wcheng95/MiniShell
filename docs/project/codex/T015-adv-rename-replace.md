# T015 — Fix ADV rename replacement semantics

Status: READY

## Bug

On Cardputer ADV, MiniFT8 exits to MiniShell with `ft8: application error` and
`app: ft8 returned 7` after setting changes such as `O -> 2`, `O -> 3`,
`O -> 5 -> 3`, and `O -> 5 -> 4`.

These actions all mutate configuration and call `app_save_config()`.

## Root cause

`ft8_main` return code 7 means `app_controller_apply_action()` returned false.
The affected actions reach:

    app_save_config()
      -> config_service_serialize()
      -> storage_service_write_text_atomic()
           write station.txt.tmp
           sync + close
           rename(station.txt.tmp, station.txt)

MiniShell Filesystem requires rename(old,new) to replace an existing regular
destination. Linux POSIX rename does this. Current ADV `fs_rename()` simply calls
POSIX `rename()`, which ESP-IDF FAT VFS forwards to FatFs `f_rename()`.
FatFs refuses an existing destination with FR_EXIST/EEXIST.

So safe-save works when the destination is absent, but fails once station.txt
already exists. The failure propagates back to FT8 as result 7.

## Ownership

Fix the ADV Filesystem backend, not FT8:

    MiniFT8 storage_service
        -> MiniShell Filesystem replacement contract
        -> ADV filesystem backend   <-- fix here
        -> ESP-IDF VFS / FatFs

Do not special-case station.txt.

## Required behavior

ADV backend rename must satisfy the MiniShell replacement contract for same-volume
regular files:

- destination absent: normal native rename;
- destination existing regular file: replace it;
- cross-volume: remain MINI_ERR_UNSUPPORTED;
- errors: map through existing mini_result_t policy.

The portable Filesystem service already checks source/destination type, normalized
identity, root protection, writable-handle ownership, and quota accounting.

## Failure-safety requirement

Do NOT implement only:

    unlink(new)
    rename(old,new)

because if the second step fails, the previous committed destination is destroyed.
This would break the safe-save expectation used by MiniFT8 configuration/logging.

If FATFS needs replacement emulation, use a backend-private same-volume backup and
rollback sequence or an equivalently safe mechanism.

Required ordinary-failure invariant:

    before replacement:
        old = new content
        new = previous committed content

    if replacement reports failure before commit:
        previous committed destination remains present/recoverable

Power-loss atomicity may remain a FAT/backend limitation; do not claim POSIX crash
guarantees that FatFs cannot provide.

## Suggested algorithm

Codex may refine after inspecting local ESP-IDF 5.5.x/FatFs source, but a valid
shape is:

1. Try native rename(old,new).
   - success: done
   - non-EEXIST: return mapped error
2. Choose an unused backend-private backup path on the same volume.
3. rename(new,backup).
   - failure: old/new unchanged; return error
4. rename(old,new).
   - success: remove backup and return OK
   - failure: rollback rename(backup,new); return mapped error if rollback succeeds;
     return IO/fail loudly if rollback itself fails.

Never overwrite or delete an unrelated existing backup candidate; choose an unused
name. Do not expose backup paths through the public API.

## Testability

Prefer a small private helper with injected/native operation callbacks so the
replacement state machine can be host-tested without ESP-IDF. Do not make it public.

## Required regression tests

Cover at least:

1. destination absent: one native rename succeeds;
2. destination exists: initial rename reports EEXIST;
3. destination -> backup succeeds;
4. source -> destination succeeds;
5. backup cleanup occurs;
6. backup collision is skipped, never overwritten;
7. initial non-EEXIST error returns without destructive steps;
8. destination -> backup failure preserves destination;
9. source -> destination failure rolls back the destination;
10. rollback failure maps to IO/fails loudly;
11. cross-volume policy remains unsupported.

Keep portable Filesystem rename-replacement tests passing.

## MiniFT8 regression

Add or extend a focused regression proving a setting action can save over an
existing station.txt through ADV-like no-native-replace behavior. The shared save
path is enough if the controller action mapping is already covered elsewhere.

Do not change UI behavior.

## ADV hardware validation

After supervisor review:

1. flash T015 ADV firmware;
2. ensure `/flash/ft8/station.txt` already exists;
3. launch `ft8`;
4. exercise `O -> 2`, `O -> 3`, `O -> 5 -> 3`, `O -> 5 -> 4`;
5. confirm FT8 remains running after each action;
6. quit normally;
7. inspect `/flash/ft8/station.txt` and confirm values persisted;
8. verify no backend-private backup/tmp residue remains after successful saves.

No QMX/RX hardware is required.

## Scope

Expected production change: `platform/adv/adv_filesystem.c`.
Expected private helper/tests as needed.

Do not change FT8 UI/action semantics, storage_service public behavior, the portable
Filesystem contract, Linux backend, or public MiniShell API.

## Local test gate

Run:

    git status --short
    cmake -S . -B build-linux
    cmake --build build-linux -j"$(nproc)"
    ctest --test-dir build-linux -R 'filesystem|storage|ft8.*config|architecture' --output-on-failure
    cmake -S tests/unit -B /tmp/T015-build-unit
    cmake --build /tmp/T015-build-unit -j"$(nproc)"
    ctest --test-dir /tmp/T015-build-unit --output-on-failure
    source ~/projects/esp-idf/export.sh
    idf.py -C platform/adv build
    git diff --check
    ctest --test-dir build-linux --output-on-failure

The two accepted Linux baseline failures may remain. No Actions wait.

## Acceptance criteria

- root cause recorded as ADV/FatFs destination-exists rename mismatch;
- fix lives in ADV filesystem backend, not FT8;
- existing regular destination is replaced on success;
- ordinary failed replacement preserves/restores previous destination when possible;
- naive destructive unlink-first implementation avoided;
- backup collision cannot overwrite user data;
- cross-volume remains unsupported;
- portable Filesystem contract unchanged;
- focused replacement helper tests pass;
- existing filesystem/storage tests pass;
- ADV firmware builds;
- no UI/action behavior changes;
- ADV hardware test confirms all four reported O-screen paths no longer exit FT8;
- station.txt changes persist;
- successful saves leave no backup/tmp residue;
- no unrelated cleanup.

## Branch workflow

Use `codex/T015-adv-rename-replace`.

Codex handoff: inspect local ESP-IDF/FatFs implementation, implement only T015,
run local tests and ADV build, set Status REVIEW, commit/push, return SHA, no PR,
no Actions wait.

Supervisor reviews `main..<SHA>`. If clean, move to TESTING for the ADV reproduction.
Do not merge until hardware confirmation.

## Codex implementation notes

### Implementation summary
### Root-cause confirmation
### ADV replacement algorithm
### Failure/rollback behavior
### Tests added
### Files changed
### Local tests/build results
### Hardware validation still required
### Known limitations / risks
### Commit

## Supervisor review

Supervisor reviews the actual diff, replacement state machine and failure behavior.

## Architect hardware result

Record the ADV O-screen validation here before COMPLETE.
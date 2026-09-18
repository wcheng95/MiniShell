# T015 — Fix ADV rename replacement semantics

Status: REVIEW

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

Implemented the same-volume replacement contract in the ADV Filesystem backend
using a private, callback-tested backup/rollback helper. No FT8 source, public API,
portable Filesystem policy, Linux backend, configuration format or UI/action
semantics changed. No task deviations. The shared FT8 safe-save path is tested
against the actual helper with emulated FatFs no-replace behavior.

### Root-cause confirmation

Inspected local ESP-IDF **v5.5.4** (`git describe --tags --always`) before editing:

- `components/fatfs/vfs/vfs_fat.c:873`: `vfs_fat_rename` calls `f_rename` under
  the FAT context lock, then maps its result to errno.
- The same file, line 332: `FR_EXIST` maps to `EEXIST`.
- `components/fatfs/src/ff.c:5238`: an existing FAT/FAT32 target other than the
  source itself yields `FR_EXIST`; registration follows only for `FR_NO_FILE`.
- Original `platform/adv/adv_filesystem.c:266` forwarded native rename directly.
- Unchanged `storage_service_write_text_atomic` writes/syncs/closes the temp and
  uses Filesystem rename to commit, explaining failure on an existing station file.

### ADV replacement algorithm

The helper takes native rename/stat/remove operations returning errno values,
plus an internal diagnostic callback. It allocates no heap and uses a bounded
512-byte stack path.

1. Reject cross-volume with EXDEV, mapped by the existing ADV policy to
   `MINI_ERR_UNSUPPORTED`; perform no native operations.
2. Attempt native rename. Success returns immediately; only EEXIST enters fallback.
3. Search up to 256 same-directory `.msr0000.bak` through `.msr00ff.bak` candidates.
   Skip case-insensitive source/destination identity and all existing candidates.
   Stat also detects FAT short-name aliases. Any non-ENOENT stat error stops safely.
4. Rename destination to the unused backup. Native FatFs no-replace semantics also
   protect a candidate created after stat; EEXIST advances to another candidate.
5. Rename source to destination. On success, remove backup and return success.
   On failure, restore backup to destination and return the original mapped error.

Private names/callbacks are not new public API or application policy. The existing
native-path/mount validation and portable source/destination/type/handle/quota
checks remain unchanged.

### Failure/rollback behavior

No unlink of the committed destination precedes installation. Initial/stat/backup
move failures preserve source and destination. An ordinary install failure restores
the previous destination. If rollback itself fails, return EIO (`MINI_ERR_IO`) and
emit an internal stderr diagnostic identifying the retained backup; the previous
bytes remain recoverable there and are never unlinked by the helper.

Successful source installation is the commit point. If backup cleanup fails,
report the residue but return OK: returning failure after commit could cause a
caller to duplicate a successfully persisted log record on retry. No automatic
sweep deletes old backups, since they may be recovery data or unrelated files.
Normal successful saves remove their own backup and consume the source temp.

### Tests added

`adv_filesystem_rename_unit` compiles the private helper and unchanged production
FT8 storage_service. Its in-memory filesystem enforces FAT-like case-insensitive,
no-replace rename semantics and asserts contents, names, calls and diagnostics.
Coverage includes:

- absent destination: one native rename;
- existing destination: EEXIST, backup move, install and cleanup;
- existing backup collision and a collision arising after stat;
- initial non-EEXIST error, candidate-stat error and backup-move failure;
- install failure with successful rollback;
- rollback failure: EIO, diagnostic, retained previous contents in backup;
- cleanup failure after commit: success plus diagnostic and retained backup;
- cross-volume rejection before native operations;
- exhausted candidate names, case-insensitive operand aliases, source matching
  a backup candidate, and path-capacity failure;
- two shared safe-saves over existing `station.txt`, confirming changed settings
  text and no backup/temp residue;
- failed shared save restoring the prior settings and removing its source temp.

The shared save path is the regression boundary; no UI mapping was changed.

### Files changed

- `platform/adv/adv_filesystem.c`: native adapters, diagnostic and helper integration.
- `platform/adv/adv_filesystem_rename.h`: private bounded replacement state machine.
- `tests/adv_filesystem_rename_test.c`: failure injection and FT8 shared-save tests.
- `CMakeLists.txt`: focused host target/CTest registration.
- This task packet: REVIEW status and handoff evidence.

### Local tests/build results

```bash
git status --short
cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux -R 'filesystem|storage|ft8.*config|architecture' --output-on-failure
cmake -S tests/unit -B /tmp/T015-build-unit
cmake --build /tmp/T015-build-unit -j"$(nproc)"
ctest --test-dir /tmp/T015-build-unit --output-on-failure
source /home/wei/projects/esp-idf/export.sh
idf.py -C platform/adv build
git diff --check
PYTHONDONTWRITEBYTECODE=1 ctest --test-dir build-linux --output-on-failure
```

Linux configure/build passed. Focused CTest **9/9 PASS**. Portable unit suite
**14/14 PASS**, including Filesystem rename-replacement tests. Full Linux suite
**35/37 PASS**, with only accepted `linux_audio` diagnostic-substring and
`linux_ft8` rotated-queue baseline failures. After adding candidate-stat failure
coverage, rebuilt and reran `ctest --test-dir build-linux -R adv_filesystem_rename
--output-on-failure`: PASS. Final whitespace check passed.

Real ADV firmware build passed against resolved local ESP-IDF v5.5.4 dependencies.
`platform/adv/build/minishell_adv.bin`: **0xa3c40 bytes**, app partition
**0x5f0000**, **0x54c3c0 bytes (89%) free**. No flashing, PR or Actions wait performed.

### Hardware validation still required

After supervisor review, flash the built ADV firmware and follow the eight-step
hardware procedure above. All four O-screen actions must stay in FT8, normal quit
must succeed, existing station values must persist with edits, and successful saves
must leave no `.msr*.bak` or station temp residue. These are pending hardware
acceptance criteria; do not merge or mark COMPLETE before architect confirmation.

### Known limitations / risks

This is ordinary-failure recovery, not power-loss atomicity or a filesystem journal.
FAT media failure may prevent rollback or cleanup; the backup is retained and its
path reported for recovery. A crash between moves may leave destination missing
and backup present; no boot-time recovery is introduced. Existing backups are never
reused destructively; exhausting 256 candidates returns EXISTS without mutation.
The multi-operation sequence assumes no concurrent namespace mutation by other
writers/USB hosts; native per-operation FAT locking does not make the sequence a
transaction. Host tests simulate ordinary operation failures, not torn media writes
or device removal. The two accepted Linux test failures remain outside scope.

### Commit

One bounded commit on `codex/T015-adv-rename-replace`, titled
`T015: preserve ADV destination during rename replacement`. The pushed SHA is
returned in the handoff; these notes are part of that commit.

## Supervisor review

Supervisor reviews the actual diff, replacement state machine and failure behavior.

## Architect hardware result

Record the ADV O-screen validation here before COMPLETE.

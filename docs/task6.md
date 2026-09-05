# Task 6 - Filesystem Namespace Commands — COMPLETE

## Goal

Task 6 completes the minimum Stage-A file-management environment with four
independent runtime applications:

```text
mv
rm
mkdir
rmdir
```

Unlike `cp`, these commands require namespace-changing operations that were
intentionally deferred from the original Filesystem ABI. Task 6 therefore uses
real application requirements to drive one small append-only ABI extension.

## Public Filesystem ABI extension

The following function pointers are appended after the original `stat` field:

```c
mini_result_t (*rename)(const char *old_path, const char *new_path);
mini_result_t (*remove_file)(const char *path);
mini_result_t (*mkdir)(const char *path);
mini_result_t (*rmdir)(const char *path);
```

One new shared result code is added because `rmdir` needs a portable distinction
between an empty-directory removal failure and a non-empty directory:

```c
#define MINI_ERR_NOT_EMPTY ((mini_result_t)-16)
```

The top-level ABI generation remains unchanged. This is compatible append-only
growth and is discovered through `mini_fs_api_t.struct_size`.

Platforms that provide the original Filesystem operations but not the new
namespace hooks remain valid Filesystem providers. The new calls return
`MINI_ERR_UNSUPPORTED` when the corresponding backend hook is absent.

## V1 command semantics

### `mv`

```text
mv <source> <destination>
```

- source must be one regular file;
- operation is a true backend rename, not copy + delete;
- destination must not already exist;
- same normalized source/destination path is a successful no-op;
- directory moves are out of scope;
- cross-filesystem moves are unsupported unless a future backend can provide a
  true rename operation.

The no-overwrite rule is deliberate. MiniShell does not silently destroy an
existing destination on the non-journaled FAT filesystem.

### `rm`

```text
rm <file>
```

- removes one regular file;
- directories are rejected;
- no recursive mode;
- no wildcard expansion.

### `mkdir`

```text
mkdir <directory>
```

- creates one directory;
- parent must already exist;
- no `-p` recursive parent creation;
- existing path returns `MINI_ERR_EXISTS`.

### `rmdir`

```text
rmdir <directory>
```

- removes one empty directory;
- regular files are rejected;
- non-empty directory returns `MINI_ERR_NOT_EMPTY`;
- root removal is rejected;
- no recursive deletion.

## Service-layer policy

Path normalization and portable policy stay in the resident Filesystem service,
not in individual apps or platform backends.

The service:

- normalizes absolute MiniShell paths before namespace changes;
- prevents mutation of `/`;
- restricts `rename()` and `remove_file()` to regular files;
- makes normalized same-file rename a no-op;
- rejects an existing rename destination before invoking the backend;
- distinguishes file and directory removal.

This ensures the same app-visible behavior on future storage backends.

## Tab5 / FATFS backend

The Tab5 backend maps the new hooks to the ESP-IDF VFS operations:

```text
rename()
unlink()
mkdir()
rmdir()
```

ESP-IDF 5.5.4/FatFS maps FatFS `FR_DENIED` to `EACCES`, and FatFS also uses
`FR_DENIED` when attempting to remove a non-empty directory. Therefore the Tab5
backend checks directory contents before calling `rmdir()` so the public ABI can
reliably report `MINI_ERR_NOT_EMPTY` instead of leaking the FATFS-specific error
mapping.

## Compatibility

The extension preserves the established ABI prefix:

```text
open / close / read / write / seek / sync / stat
```

Old applications such as `cat.elf`, `nano.elf`, and `cp.elf` require only earlier
fields and remain compatible with the extended resident table.

New namespace applications must check `mini_fs_api_t.struct_size` through the
specific field they require before dereferencing that function pointer.

## Testing

### Host unit test

The existing `abi_filesystem_unit` additionally verifies:

- normalized file rename;
- same-normalized-path rename no-op;
- no-overwrite rename behavior;
- root rename protection;
- regular-file removal;
- directory rejection by `remove_file`;
- single-directory creation;
- existing and missing-parent mkdir errors;
- non-empty-directory rejection;
- empty-directory removal;
- file rejection by `rmdir`;
- root rmdir protection;
- optional namespace backend hooks returning `MINI_ERR_UNSUPPORTED` without
  disabling the original Filesystem service.

### ELF integration

`abi_fs.elf` performs one complete namespace lifecycle:

```text
create/write/read file
        |
        v
mkdir test directory
        |
        v
rename file into directory
        |
        v
remove file
        |
        v
rmdir empty directory
```

### Hardware validation

Real Tab5 hardware validation passed for the Stage-A namespace sequence:

```text
mkdir directory                  PASS
nano create/edit file            PASS
rmdir non-empty protection       PASS
mv regular-file rename           PASS
cat verification after mv        PASS
cp regression                    PASS
mv no-overwrite protection       PASS
rm regular files                 PASS
rmdir empty directory            PASS
final directory removal          PASS
```

A temporary Nano/terminal hang observed during validation disappeared after
rebooting the Tab5 and opening a fresh terminal on pc-1; Nano then operated
normally. No MiniShell/Nano regression was reproduced, and the speculative
foreground-stack diagnostic was reverted.

## Completion criteria

Task 6 is complete:

- the append-only Filesystem ABI extension builds cleanly;
- the Filesystem unit coverage includes the namespace operations;
- the updated Filesystem ELF integration path is implemented;
- all four runtime apps build and load independently;
- `mv` rename/no-overwrite behavior passes on Tab5;
- `rm` removes files and rejects directory semantics by contract;
- `mkdir` creates one directory;
- `rmdir` rejects non-empty directories and removes empty ones;
- older Stage-A apps continue to execute correctly;
- no additional filesystem features were added beyond this concrete scope.

Task 6 also completes the planned Stage-A file environment:

```text
cat  nano  cp  mv  rm  mkdir  rmdir
```

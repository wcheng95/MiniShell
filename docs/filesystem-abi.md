# MiniShell Filesystem ABI

Status: **foundational file I/O complete; Task-6 namespace extension implemented**

## 1. Purpose

The Filesystem ABI gives runtime applications byte-oriented access to MiniShell's
logical file namespace without exposing FATFS, ESP-IDF VFS objects, libc `FILE *`,
mount objects, SD/MMC drivers, or storage-hardware details.

```text
application
    |
    | MiniShell Filesystem ABI
    v
resident filesystem service
    |
    v
platform filesystem backend
    |
    v
storage hardware
```

MiniShell owns mounts, backend objects, shared storage hardware, and logical file
handles. Applications see only MiniShell paths, handles, structures, and result
codes.

## 2. Compatibility model

The original Filesystem table prefix is:

```text
open
close
read
write
seek
sync
stat
```

Task 6 appends four namespace-changing operations:

```text
rename
remove_file
mkdir
rmdir
```

No original field is reordered or given a new meaning. The top-level MiniShell ABI
generation therefore remains unchanged.

Applications check `mini_fs_api_t.struct_size` only through the last field they
actually require. An old application that requires only `read`, for example,
continues to run against a newer table.

A newer MiniShell port may provide the original file operations while omitting
backend support for one or more appended namespace operations. In that case the
service remains available and the unsupported operation returns
`MINI_ERR_UNSUPPORTED`.

## 3. File handle

```c
typedef uint32_t mini_file_t;
#define MINI_FILE_INVALID ((mini_file_t)0u)
```

A handle is an opaque application-visible token. Its value must not be treated as
a pointer, libc descriptor, FATFS object, or platform handle.

A valid handle belongs to the current foreground application until it is closed
or reclaimed during application teardown.

## 4. Paths

Paths are NUL-terminated UTF-8 byte strings in the MiniShell logical namespace.

Examples:

```text
/sd/notes.txt
/flash/config.ini
```

Rules:

- application paths are absolute and begin with `/`;
- `/` is the path separator;
- repeated separators are collapsed;
- `.` components are ignored;
- `..` may move upward but may not escape MiniShell root;
- backend syntax such as FATFS `0:` is never exposed;
- no fixed public `MINI_PATH_MAX` is part of the ABI;
- UTF-8 is passed as bytes; Unicode normalization is not promised;
- case sensitivity is backend-dependent, so portable apps must not depend on
  names that differ only by case.

Namespace-changing calls use the same normalization rules as `open()` and
`stat()`.

## 5. Open flags

```c
#define MINI_FS_READ    (1u << 0)
#define MINI_FS_WRITE   (1u << 1)
#define MINI_FS_CREATE  (1u << 2)
#define MINI_FS_EXCL    (1u << 3)
#define MINI_FS_TRUNC   (1u << 4)
#define MINI_FS_APPEND  (1u << 5)
```

Rules:

- at least one of READ or WRITE is required;
- CREATE requires WRITE;
- EXCL requires CREATE;
- TRUNC requires WRITE;
- APPEND requires WRITE;
- unknown flag bits return `MINI_ERR_INVALID`.

Semantics:

```text
READ     open an existing file for reading
WRITE    open an existing file for writing without truncating
CREATE   create if absent; preserve if present
EXCL     with CREATE, fail when already present
TRUNC    reduce the opened file to zero length
APPEND   every write begins at EOF
```

## 6. Seek origins

```c
#define MINI_FS_SEEK_SET 0u
#define MINI_FS_SEEK_CUR 1u
#define MINI_FS_SEEK_END 2u
```

A resulting position below zero is invalid. V1 does not promise sparse-file
creation by seeking past EOF. A failed seek leaves the previous position
unchanged.

## 7. Stat

```c
#define MINI_FS_TYPE_FILE       1u
#define MINI_FS_TYPE_DIRECTORY  2u

typedef struct {
    uint32_t struct_size;
    uint32_t type;
    uint64_t size;
} mini_fs_stat_t;
```

The caller zero-initializes the structure and sets `struct_size`. For directories,
`size` is backend-dependent and has no portable meaning.

V1 does not expose timestamps, ownership, permissions, links, or backend-specific
attributes.

## 8. Service table

```c
typedef struct {
    uint32_t struct_size;

    mini_result_t (*open)(const char *path,
                          uint32_t flags,
                          mini_file_t *out_file);

    mini_result_t (*close)(mini_file_t file);

    mini_result_t (*read)(mini_file_t file,
                          void *buffer,
                          uint32_t size,
                          uint32_t *out_read);

    mini_result_t (*write)(mini_file_t file,
                           const void *buffer,
                           uint32_t size,
                           uint32_t *out_written);

    mini_result_t (*seek)(mini_file_t file,
                          int64_t offset,
                          uint32_t origin,
                          uint64_t *out_position);

    mini_result_t (*sync)(mini_file_t file);

    mini_result_t (*stat)(const char *path,
                          mini_fs_stat_t *out_stat);

    /* Task-6 append-only extension. */
    mini_result_t (*rename)(const char *old_path,
                            const char *new_path);

    mini_result_t (*remove_file)(const char *path);
    mini_result_t (*mkdir)(const char *path);
    mini_result_t (*rmdir)(const char *path);
} mini_fs_api_t;
```

## 9. `open()`

```c
mini_result_t open(const char *path,
                   uint32_t flags,
                   mini_file_t *out_file);
```

`path` and `out_file` are required. `*out_file` is set to
`MINI_FILE_INVALID` before the attempt. The path must resolve to a regular file,
not a directory.

On success the initial file position is byte zero. APPEND semantics affect writes.

## 10. `close()`

```c
mini_result_t close(mini_file_t file);
```

After `close()` returns, the logical handle is invalid even if backend close
reported an error. Applications that care about persistence errors should call
`sync()` explicitly before close.

Closing an invalid, stale, already-closed, or foreign handle returns
`MINI_ERR_BAD_HANDLE`.

## 11. `read()`

```c
mini_result_t read(mini_file_t file,
                   void *buffer,
                   uint32_t size,
                   uint32_t *out_read);
```

- `out_read` is required and initialized to zero;
- `buffer` is required when `size > 0`;
- the handle must have READ access;
- partial reads are valid;
- EOF is `MINI_OK` plus `*out_read == 0`;
- zero-byte reads succeed.

Callers needing an exact count must loop.

## 12. `write()`

```c
mini_result_t write(mini_file_t file,
                    const void *buffer,
                    uint32_t size,
                    uint32_t *out_written);
```

- `out_written` is required and initialized to zero;
- `buffer` is required when `size > 0`;
- the handle must have WRITE access;
- partial writes are valid;
- zero-byte writes succeed;
- for `size > 0`, `MINI_OK` must report positive progress;
- APPEND writes begin at EOF regardless of the current seek position.

Callers needing all bytes written must loop.

## 13. `seek()`

```c
mini_result_t seek(mini_file_t file,
                   int64_t offset,
                   uint32_t origin,
                   uint64_t *out_position);
```

`out_position` is required and initialized to zero. On success it receives the
new absolute position. On failure the old position remains unchanged.

## 14. `sync()`

```c
mini_result_t sync(mini_file_t file);
```

`sync()` requests that pending file data and relevant metadata be flushed as far
as the backend can provide. `MINI_OK` does not promise atomic replacement,
journaling, or survival under every sudden-power-loss scenario.

## 15. `stat()`

```c
mini_result_t stat(const char *path,
                   mini_fs_stat_t *out_stat);
```

A missing path returns `MINI_ERR_NOT_FOUND`. The output structure must include the
minimum established `mini_fs_stat_t` prefix.

## 16. `rename()`

```c
mini_result_t rename(const char *old_path,
                     const char *new_path);
```

Task-6 V1 semantics are deliberately narrower than POSIX rename:

- source must be a regular file;
- directories are rejected with `MINI_ERR_IS_DIR`;
- source and destination are normalized before comparison;
- the MiniShell root `/` may not be renamed or used as the destination;
- renaming a regular file to the same normalized path is a successful no-op;
- destination must not already exist;
- an existing destination returns `MINI_ERR_EXISTS` and is not modified;
- no copy-and-delete fallback is performed;
- cross-filesystem rename may return `MINI_ERR_UNSUPPORTED`.

The no-overwrite rule is a deliberate safety boundary for the initial
non-journaled FAT backend. A future explicit replace operation can be designed if
a real application requires it.

## 17. `remove_file()`

```c
mini_result_t remove_file(const char *path);
```

- removes exactly one regular file;
- directory operands return `MINI_ERR_IS_DIR`;
- `/` cannot be removed as a file;
- missing path returns `MINI_ERR_NOT_FOUND`;
- recursive deletion is not part of this operation.

## 18. `mkdir()`

```c
mini_result_t mkdir(const char *path);
```

- creates exactly one directory;
- the parent must already exist and be a directory;
- an existing path returns `MINI_ERR_EXISTS`;
- `mkdir("/")` returns `MINI_ERR_EXISTS`;
- recursive `-p` behavior is not part of the ABI.

## 19. `rmdir()`

```c
mini_result_t rmdir(const char *path);
```

- removes exactly one empty directory;
- regular-file operands return `MINI_ERR_NOT_DIR`;
- non-empty directory returns `MINI_ERR_NOT_EMPTY`;
- the root `/` is protected and returns `MINI_ERR_ACCESS`;
- recursive removal is not part of the ABI.

The Tab5/FATFS backend explicitly detects directory contents before calling the
ESP-IDF VFS `rmdir()` path because FatFS uses `FR_DENIED` for a non-empty
directory and ESP-IDF maps `FR_DENIED` to `EACCES`. That backend quirk must not
leak through the public MiniShell result namespace.

## 20. Ownership and teardown

MiniShell owns backend file resources while an application owns its logical
handles.

If an app returns with open handles, MiniShell reclaims them before unloading the
ELF:

```text
app returns
   -> reclaim open file resources
   -> unload ELF
   -> shell resumes
```

Automatic teardown is a safety net, not a replacement for normal close/error
handling.

Namespace operations are synchronous and do not create persistent MiniShell
handles.

## 21. Shared errors

Filesystem calls use `mini_result_t`. Relevant values include:

```text
MINI_ERR_INVALID
MINI_ERR_NOT_FOUND
MINI_ERR_EXISTS
MINI_ERR_BAD_HANDLE
MINI_ERR_ACCESS
MINI_ERR_IO
MINI_ERR_NO_SPACE
MINI_ERR_TOO_MANY_OPEN
MINI_ERR_NAME_TOO_LONG
MINI_ERR_UNSUPPORTED
MINI_ERR_NOT_DIR
MINI_ERR_IS_DIR
MINI_ERR_NO_MEMORY
MINI_ERR_NOT_EMPTY
```

Backend values such as `errno`, FATFS `FRESULT`, or `esp_err_t` never cross the
public ABI.

## 22. Deferred functionality

Still deliberately out of scope:

```text
opendir / readdir / closedir
current working directory
wildcards / globbing
permissions / ownership
links
locking
memory mapping
asynchronous I/O
mount / unmount
filesystem-specific controls
recursive copy/remove
atomic generic replace
storage-capacity/free-space query
```

`df` is expected to drive the future storage-capacity/free-space extension.

## 23. Verification

### Host unit tests

The filesystem service tests cover:

- path normalization and root-escape rejection;
- open flag combinations;
- create/exclusive/truncate/append behavior;
- logical handle ownership/staleness/teardown;
- permissions on read/write handles;
- partial reads and writes;
- EOF and zero-byte operations;
- seek semantics and failed-seek position preservation;
- sync;
- stat and extensible structure size;
- regular-file rename and normalized same-path no-op;
- no-overwrite rename;
- root rename protection;
- regular-file deletion and directory rejection;
- mkdir existing/missing-parent cases;
- non-empty and empty rmdir cases;
- optional namespace backend hooks returning `MINI_ERR_UNSUPPORTED` without
  disabling the foundational Filesystem service.

### Runtime-loaded ELF integration

`abi_fs.elf` verifies the original open/read/write/stat path and then performs:

```text
mkdir -> rename regular file -> remove_file -> rmdir
```

### Hardware validation

The Tab5 test must prove the same namespace lifecycle on the real SD/FATFS backend
and confirm that older applications using the established table prefix still run.

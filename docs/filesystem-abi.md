# MiniShell Filesystem ABI v0

Status: **Task 1 design contract; provisional until implementation, unit tests, ELF integration, and hardware validation pass**

## 1. Purpose

The Filesystem ABI gives applications byte-oriented access to MiniShell's logical
file namespace without exposing FATFS, ESP-IDF VFS objects, libc `FILE *`, mount
objects, SD/MMC drivers, or storage hardware details.

```text
application
    |
    | MiniShell Filesystem ABI
    v
MiniShell filesystem service
    |
    v
platform filesystem/backend
    |
    v
storage hardware
```

MiniShell owns the underlying storage hardware, mounts, backend objects, and file
resources. Applications operate only on paths and opaque MiniShell file handles.

## 2. V0 scope

Filesystem ABI v0 contains:

```text
open
close
read
write
seek
sync
stat
```

Deferred operations include:

```text
readline / write_all
opendir / readdir / closedir
mkdir / rmdir
rename
remove / unlink
current working directory
wildcards / globbing
permissions / ownership
links
locking
memory mapping
asynchronous I/O
mount / unmount
filesystem-specific control operations
```

These may be added later without changing the v0 operations.

## 3. File handle

```c
typedef uint32_t mini_file_t;
#define MINI_FILE_INVALID ((mini_file_t)0u)
```

`mini_file_t` is an opaque application-visible token. Its numeric value has no
portable meaning and must not be interpreted as a pointer, libc descriptor,
FATFS object, or backend handle.

A valid handle belongs to the currently running application and remains valid
until it is closed or reclaimed during normal application teardown.

## 4. Paths

Paths are NUL-terminated UTF-8 byte strings in the MiniShell logical namespace.

Examples:

```text
/sd/notes.txt
/flash/config.ini
```

V0 rules:

- application paths are absolute and begin with `/`;
- `/` is the path separator;
- repeated `/` characters are treated as one separator;
- `.` components are ignored;
- `..` may move upward but may not escape the MiniShell root;
- FATFS drive syntax such as `0:` is never exposed;
- no fixed `MINI_PATH_MAX` is part of the ABI;
- UTF-8 is passed as bytes; Unicode normalization is not promised;
- case sensitivity is backend-dependent, so portable applications must not rely
  on names that differ only by case.

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
CREATE   create if absent; preserve if already present
EXCL     with CREATE, fail if the path already exists
TRUNC    reduce the opened file to zero length
APPEND   every write begins at EOF regardless of seek position
```

Typical combinations:

```text
READ
READ | WRITE
WRITE | CREATE | TRUNC
WRITE | CREATE | APPEND
WRITE | CREATE | EXCL
```

## 6. Seek origins

```c
#define MINI_FS_SEEK_SET 0u
#define MINI_FS_SEEK_CUR 1u
#define MINI_FS_SEEK_END 2u
```

A resulting position below zero is invalid. V0 does not promise sparse-file
creation by seeking beyond EOF.

For an APPEND handle, `seek()` may change the current/read position, but every
subsequent `write()` still begins at EOF.

## 7. Stat structure

```c
#define MINI_FS_TYPE_FILE       1u
#define MINI_FS_TYPE_DIRECTORY  2u

typedef struct {
    uint32_t struct_size;
    uint32_t type;
    uint64_t size;
} mini_fs_stat_t;
```

The caller zero-initializes the structure and sets `struct_size` before calling
`stat()`. MiniShell writes only fields covered by the caller's structure size.

For directories, `size` is backend-dependent and has no portable meaning. V0 does
not expose timestamps, permissions, ownership, or backend-specific attributes.

## 8. Service table

```c
typedef struct {
    uint32_t struct_size;

    mini_result_t (*open)(
        const char *path,
        uint32_t flags,
        mini_file_t *out_file);

    mini_result_t (*close)(mini_file_t file);

    mini_result_t (*read)(
        mini_file_t file,
        void *buffer,
        uint32_t size,
        uint32_t *out_read);

    mini_result_t (*write)(
        mini_file_t file,
        const void *buffer,
        uint32_t size,
        uint32_t *out_written);

    mini_result_t (*seek)(
        mini_file_t file,
        int64_t offset,
        uint32_t origin,
        uint64_t *out_position);

    mini_result_t (*sync)(mini_file_t file);

    mini_result_t (*stat)(
        const char *path,
        mini_fs_stat_t *out_stat);
} mini_fs_api_t;
```

The table is append-only after ABI stabilization.

## 9. `open()`

```c
mini_result_t open(
    const char *path,
    uint32_t flags,
    mini_file_t *out_file);
```

Requirements:

- `path` and `out_file` are non-NULL;
- `*out_file` is set to `MINI_FILE_INVALID` before the attempt;
- flags satisfy the rules above;
- the path resolves to a regular file, not a directory.

On success, the initial file position is byte zero. APPEND semantics apply only
to writes.

## 10. `close()`

```c
mini_result_t close(mini_file_t file);
```

After `close()` returns, the supplied handle is invalid regardless of whether the
return value is success or an error. A close error may indicate final flush or
backend-close failure; it does not preserve the old handle.

Applications that care about persistence errors should call `sync()` explicitly
before `close()`.

Closing an invalid, already-closed, or foreign handle returns
`MINI_ERR_BAD_HANDLE`.

## 11. `read()`

```c
mini_result_t read(
    mini_file_t file,
    void *buffer,
    uint32_t size,
    uint32_t *out_read);
```

Semantics:

- `out_read` is non-NULL;
- `buffer` is non-NULL when `size > 0`;
- the handle has READ access;
- `*out_read` is set to zero before the attempt;
- partial reads are valid;
- EOF is `MINI_OK` plus `*out_read == 0`;
- `size == 0` is a successful zero-byte operation;
- on success, position advances by bytes actually read.

Applications requiring an exact count must loop.

## 12. `write()`

```c
mini_result_t write(
    mini_file_t file,
    const void *buffer,
    uint32_t size,
    uint32_t *out_written);
```

Semantics:

- `out_written` is non-NULL;
- `buffer` is non-NULL when `size > 0`;
- the handle has WRITE access;
- `*out_written` is set to zero before the attempt;
- partial writes are valid;
- `size == 0` is a successful zero-byte operation;
- for `size > 0`, `MINI_OK` must report positive progress;
- APPEND writes always begin at EOF.

Applications requiring all bytes to be written must loop.

## 13. `seek()`

```c
mini_result_t seek(
    mini_file_t file,
    int64_t offset,
    uint32_t origin,
    uint64_t *out_position);
```

`out_position` is non-NULL and is set to zero before the attempt.

On success, it receives the resulting absolute byte position.

**On failure, the file position remains unchanged.** This is part of the v0
contract so callers never have to guess whether an unsuccessful seek moved the
stream.

## 14. `sync()`

```c
mini_result_t sync(mini_file_t file);
```

`sync()` asks MiniShell to flush pending file data and relevant metadata as far
as the backend can provide. `MINI_OK` does not promise atomic replacement,
journaling, or survival of every sudden-power-loss scenario.

V0 provides file-level sync only.

## 15. `stat()`

```c
mini_result_t stat(
    const char *path,
    mini_fs_stat_t *out_stat);
```

`path` and `out_stat` are non-NULL. The caller supplies a valid `struct_size`.
A missing path returns `MINI_ERR_NOT_FOUND`.

## 16. Ownership and teardown

MiniShell owns the backend file resources while the application owns its logical
handles.

If an app returns with open handles, MiniShell reclaims them before unloading the
ELF. Automatic cleanup is a safety net, not a substitute for normal `close()` and
error handling.

```text
app returns
   -> reclaim open file resources
   -> unload ELF
   -> shell resumes
```

## 17. Shared errors

Filesystem calls use the common `mini_result_t` namespace defined in
`docs/abi-foundation.md`. Backend-native values such as `errno`, FATFS `FRESULT`,
or `esp_err_t` never cross the public ABI.

## 18. Extension rule

V0 fields and meanings are append-only after stabilization. Namespace-changing,
directory, convenience, asynchronous, or backend-specific functionality must be
added explicitly rather than changing existing operation semantics.

A future robust editor-save flow may justify `rename()` and `remove()` so a new
file can be written, synchronized, and then substituted for the old file.

## 19. Verification requirements

### 19.1 Unit tests — primary

The Filesystem service must have comprehensive unit tests using a fake or
in-memory backend that can deliberately return partial I/O and specific failures.
They should verify at least:

1. path normalization for repeated `/`, `.`, and `..` without escaping root;
2. invalid/relative paths and backend path-limit translation;
3. all valid and invalid open-flag combinations;
4. create/exclusive/truncate/append semantics;
5. opaque-handle ownership, stale handles, foreign handles, and generation/reuse
   behavior if the implementation uses reusable slots;
6. read/write permission enforcement;
7. partial reads and partial writes;
8. EOF as `MINI_OK` plus zero bytes;
9. zero-byte read/write semantics;
10. read/write output counts initialized correctly on failures;
11. seek SET/CUR/END, negative-result rejection, and overflow/boundary cases;
12. failed seek preserving the old position;
13. APPEND writes beginning at EOF after arbitrary seeks;
14. sync success/failure translation;
15. close invalidating the logical handle even when backend close/flush reports an
    error;
16. stat file/directory/missing-path behavior and `struct_size` compatibility;
17. per-app handle ownership and teardown reclamation;
18. repeated open/close/teardown cycles without leaking bookkeeping state.

### 19.2 Runtime-loaded ELF integration test

`abi_fs.elf` should prove the real ABI/runtime path with a representative subset:

1. service/table discovery;
2. open/read/close an existing file;
3. create/write/sync/close/read-back;
4. one seek case and APPEND behavior;
5. one representative invalid-handle or invalid-flag case;
6. stat through the public structure;
7. leave one handle open and return so teardown cleanup is exercised;
8. repeat launch/run/exit.

The ELF integration test does not need to duplicate the full unit matrix.

### 19.3 Hardware/platform validation

On Tab5, validate the real SD/FATFS backend, including actual persistence across
close/remount/restart where relevant and representative physical-media/backend
errors that can be reproduced safely.

The ABI remains provisional until the unit suite, focused ELF integration test,
and required hardware validation all pass.

# MiniShell Application ABI

Status: **initial design contract, not frozen**

This document defines the intended boundary between MiniShell and a normal native
application. Task 0 validated the native ELF loading and runtime binding model on
real Tab5 hardware. The ABI is still allowed to evolve while early real
applications, beginning with `med`, exercise the service boundaries.

## 1. Goals

The ABI should:

- let an app be built separately from MiniShell
- let an app be loaded and executed without rebooting
- let MiniShell remain resident while the app runs
- expose useful runtime services without exposing ESP-IDF
- keep application source portable across MiniShell platforms where practical
- allow MiniShell and apps to optimize independently
- remain small enough to understand and version

The ABI is not intended to provide process isolation or security boundaries.

## 2. Initial Native App Format

V1 native applications use ELF.

ELF provides the loader with code, data, symbol, relocation, and entry-point
information needed for runtime loading. ELF is a loading/container choice; the
MiniShell service contract should not depend on ELF-specific concepts unless the
loader requires them.

Each CPU architecture requires a compatible binary. For example, an ESP32-P4
RISC-V build and a future ESP32-S3 Xtensa build are separate ELF files even when
they are built from the same application source.

## 3. Entry Point

Initial conceptual entry point:

```c
int mini_main(const mini_api_t *api, int argc, char **argv);
```

Meaning:

- `api` points to the MiniShell runtime API table
- `argc` and `argv` contain shell command arguments
- the return value is the application's exit status

Example shell command:

```text
M$> minift8 --band 20m
```

Conceptually produces:

```text
argc = 3
argv[0] = "minift8"
argv[1] = "--band"
argv[2] = "20m"
```

Task 0 proved the current runtime binding mechanism through `mini_api_get()`.
The source-level entry model may still be refined before ABI v1 is frozen.

## 4. Versioned API Table

The preferred model is a versioned function table owned by MiniShell.

Conceptual form:

```c
typedef struct {
    uint32_t abi_version;
    uint32_t struct_size;

    const mini_system_api_t  *system;
    const mini_fs_api_t      *fs;
    const mini_time_api_t    *time;
    const mini_display_api_t *display;
    const mini_input_api_t   *input;
    const mini_audio_api_t   *audio;
} mini_api_t;
```

Only implemented services are present in the real table. The larger form above
shows the intended growth direction; it is not a commitment to implement every
service immediately.

A table makes runtime dependency direction explicit:

```text
app.elf
   |
   | function-table calls
   v
MiniShell resident services
```

It also avoids making normal applications resolve ESP-IDF or hardware-driver
symbols directly.

## 5. ABI Versioning

Every app must be able to determine whether the resident MiniShell provides a
compatible ABI.

Initial requirements:

- MiniShell exposes an ABI version
- API structures expose their size where useful
- additions should prefer backward-compatible extension
- incompatible changes require a new major ABI version
- an app that cannot run safely must fail before entering its main logic

Task 0 proved that the mechanism works. Do not freeze ABI v1 until at least one
real application has exercised the service surface.

## 6. MiniShell-Owned Types

Public headers define MiniShell types.

Examples:

```c
typedef int32_t  mini_result_t;
typedef uint32_t mini_file_t;
typedef uint32_t mini_time_ms_t;
```

Public API signatures must not require normal apps to include ESP-IDF types such
as `esp_err_t`, FreeRTOS task handles, ESP-IDF USB structures, M5Stack driver
objects, FatFs objects, or C-library `FILE *` values.

Platform-specific extension APIs may intentionally expose platform concepts, but
those are outside the portable ABI.

## 7. Initial Service Priorities

Do not attempt to define a complete OS API before real apps need it.

Task 0 needed only enough API to prove runtime binding:

```c
api->system->write("Hello from app\n");
```

Task 1 (`med`) drives the next services: filesystem plus the console/input
operations required by a terminal editor.

Likely later growth areas include:

1. system / console
2. filesystem
3. memory/status
4. time/RTC
5. display
6. input
7. audio
8. USB
9. networking
10. power/platform extensions

The order may change based on real applications.

## 8. Filesystem ABI v0

Filesystem ABI v0 is the first substantial MiniShell service contract. It is
small deliberately. It provides byte-oriented file access without exposing
FatFs, ESP-IDF VFS, littlefs, C-library `FILE *`, or any particular storage
hardware.

The initial operation set is:

```text
open
close
read
write
seek
sync
stat
```

Convenience operations such as `readline()` and `write_all()` are postponed
until a real application needs them. Directory iteration, rename, remove, mkdir,
and other namespace-modifying operations are also outside v0.

### 8.1 Dependency and ownership boundary

Normal application path:

```text
application
    |
    | mini_fs_api_t
    v
MiniShell filesystem service
    |
    | private implementation
    v
ESP-IDF VFS / FatFs / future backend
    |
    v
storage hardware
```

Applications do not normally initialize, mount, unmount, or otherwise own the
underlying storage hardware.

### 8.2 File handle

```c
typedef uint32_t mini_file_t;

#define MINI_FILE_INVALID ((mini_file_t)0u)
```

`mini_file_t` is an opaque MiniShell token. Its numeric value has no application
meaning. It must not be cast to or interpreted as a pointer, C file descriptor,
FatFs object, or backend-specific handle.

A valid handle belongs to the currently running application. It is valid until:

- the application closes it,
- MiniShell reclaims it during normal application teardown, or
- the filesystem service reports that it is no longer valid.

Applications must not persist file handles across application launches.

The internal implementation is free to use a slot table, generation counter, or
another mechanism without changing this ABI.

### 8.3 Paths

Filesystem paths are NUL-terminated UTF-8 byte strings in the MiniShell logical
namespace.

Examples:

```text
/sd/notes.txt
/flash/config.ini
```

Rules for filesystem ABI v0:

- paths passed by applications are absolute and begin with `/`
- `/` is the only path separator
- repeated `/` characters are treated as one separator
- a `.` path component is ignored
- a `..` component resolves one level upward but may not escape the MiniShell
  root
- the ABI does not expose FatFs drive syntax such as `0:`
- UTF-8 is passed as bytes; MiniShell v0 does not promise Unicode normalization
- case sensitivity may depend on the mounted backend; portable applications
  should not rely on names differing only by case
- the ABI defines no fixed `MINI_PATH_MAX`; a backend limitation is reported as
  a MiniShell error

### 8.4 Result codes

Filesystem calls return `mini_result_t`. Zero means success; negative values are
errors.

The initial portable result set is:

```c
#define MINI_OK                  ((mini_result_t)  0)
#define MINI_ERR_INVALID         ((mini_result_t) -1)
#define MINI_ERR_NOT_FOUND       ((mini_result_t) -2)
#define MINI_ERR_EXISTS          ((mini_result_t) -3)
#define MINI_ERR_BAD_HANDLE      ((mini_result_t) -4)
#define MINI_ERR_ACCESS          ((mini_result_t) -5)
#define MINI_ERR_IO              ((mini_result_t) -6)
#define MINI_ERR_NO_SPACE        ((mini_result_t) -7)
#define MINI_ERR_TOO_MANY_OPEN   ((mini_result_t) -8)
#define MINI_ERR_NAME_TOO_LONG   ((mini_result_t) -9)
#define MINI_ERR_UNSUPPORTED     ((mini_result_t)-10)
#define MINI_ERR_NOT_DIR         ((mini_result_t)-11)
#define MINI_ERR_IS_DIR          ((mini_result_t)-12)
```

These are MiniShell results, not aliases for `errno`, FatFs `FRESULT`, or
`esp_err_t`. A platform/backend implementation translates its native errors into
this set.

The result namespace is shared by MiniShell services; these names are not
filesystem-private even though filesystem ABI v0 is the first service to require
most of them.

### 8.5 Open flags

Open behavior uses explicit bit flags rather than C-library mode strings.

```c
#define MINI_FS_READ    (1u << 0)
#define MINI_FS_WRITE   (1u << 1)
#define MINI_FS_CREATE  (1u << 2)
#define MINI_FS_EXCL    (1u << 3)
#define MINI_FS_TRUNC   (1u << 4)
#define MINI_FS_APPEND  (1u << 5)
```

Rules:

- at least one of `MINI_FS_READ` or `MINI_FS_WRITE` is required
- `MINI_FS_CREATE` requires `MINI_FS_WRITE`
- `MINI_FS_EXCL` requires `MINI_FS_CREATE`
- `MINI_FS_TRUNC` requires `MINI_FS_WRITE`
- `MINI_FS_APPEND` requires `MINI_FS_WRITE`
- unknown flag bits return `MINI_ERR_INVALID`

Semantics:

```text
READ
    open an existing file for reading

WRITE
    open an existing file for writing without truncating it

CREATE
    create the file if it does not exist; preserve an existing file

EXCL
    when combined with CREATE, fail with MINI_ERR_EXISTS if the path exists

TRUNC
    if the file opens successfully, reduce its length to zero

APPEND
    every write begins at the current end of file, regardless of the current
    seek position
```

Typical combinations:

```text
MINI_FS_READ
    read an existing file

MINI_FS_READ | MINI_FS_WRITE
    edit an existing file

MINI_FS_WRITE | MINI_FS_CREATE | MINI_FS_TRUNC
    create or replace a file

MINI_FS_WRITE | MINI_FS_CREATE | MINI_FS_APPEND
    create if needed and append

MINI_FS_WRITE | MINI_FS_CREATE | MINI_FS_EXCL
    create only if absent
```

### 8.6 Seek origins

```c
#define MINI_FS_SEEK_SET 0u
#define MINI_FS_SEEK_CUR 1u
#define MINI_FS_SEEK_END 2u
```

Their meanings are:

```text
SET   offset is relative to byte position 0
CUR   offset is relative to the current file position
END   offset is relative to the current end of file
```

A resulting position below zero is invalid. Filesystem ABI v0 does not promise
sparse-file creation by seeking beyond EOF. Backends that cannot support a seek
request return `MINI_ERR_UNSUPPORTED` or another more specific MiniShell error.

For a file opened with `MINI_FS_APPEND`, seeking may change the read/current
position, but every subsequent `write()` still begins at EOF.

### 8.7 File type and stat structure

```c
#define MINI_FS_TYPE_FILE       1u
#define MINI_FS_TYPE_DIRECTORY  2u

typedef struct {
    uint32_t struct_size;
    uint32_t type;
    uint64_t size;
} mini_fs_stat_t;
```

Before calling `stat()`, the caller sets:

```c
st.struct_size = sizeof(st);
```

MiniShell writes only fields that fit inside the caller-provided structure size.
Unknown future fields can therefore be appended without changing existing field
offsets.

For a directory, `size` is backend-dependent and applications should not attach
portable meaning to it. Filesystem ABI v0 does not expose timestamps,
permissions, owners, or backend-specific attributes.

### 8.8 Filesystem service table

The agreed filesystem ABI v0 shape is:

```c
typedef struct {
    uint32_t struct_size;

    mini_result_t (*open)(
        const char *path,
        uint32_t flags,
        mini_file_t *out_file);

    mini_result_t (*close)(
        mini_file_t file);

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

    mini_result_t (*sync)(
        mini_file_t file);

    mini_result_t (*stat)(
        const char *path,
        mini_fs_stat_t *out_stat);
} mini_fs_api_t;
```

The filesystem service is exposed from the top-level API as:

```c
const mini_fs_api_t *fs;
```

### 8.9 `open()` semantics

```c
mini_result_t open(
    const char *path,
    uint32_t flags,
    mini_file_t *out_file);
```

Requirements:

- `path` and `out_file` must be non-NULL
- `*out_file` is set to `MINI_FILE_INVALID` before attempting the open
- flags must satisfy section 8.5
- the path must resolve to a regular file, not a directory

On success:

- returns `MINI_OK`
- stores a valid application-owned handle in `*out_file`
- initial file position is byte 0, except that append semantics apply to writes

Typical errors include `MINI_ERR_NOT_FOUND`, `MINI_ERR_EXISTS`,
`MINI_ERR_ACCESS`, `MINI_ERR_IS_DIR`, `MINI_ERR_TOO_MANY_OPEN`, and
`MINI_ERR_INVALID`.

### 8.10 `close()` semantics

```c
mini_result_t close(mini_file_t file);
```

`close()` releases the application-visible handle and the underlying MiniShell
resource. It may flush backend state as part of closing.

After `close()` returns, the supplied handle is invalid regardless of whether the
return value is `MINI_OK` or an error. An error means final flushing or backend
close processing failed; it does not preserve ownership of the old handle.

Closing `MINI_FILE_INVALID`, an already-closed handle, or a handle not owned by
the current application returns `MINI_ERR_BAD_HANDLE`.

Applications that care about persistence errors should call `sync()` before
`close()` so the sync result can be handled explicitly.

### 8.11 `read()` semantics

```c
mini_result_t read(
    mini_file_t file,
    void *buffer,
    uint32_t size,
    uint32_t *out_read);
```

Requirements:

- `out_read` must be non-NULL
- `buffer` must be non-NULL when `size > 0`
- the handle must have been opened with `MINI_FS_READ`

Behavior:

- `*out_read` is set to zero before attempting the read
- a successful read may return fewer bytes than requested
- EOF is **not an error**: return `MINI_OK` with `*out_read == 0`
- `size == 0` returns `MINI_OK` with `*out_read == 0`
- on success, the file position advances by the number of bytes actually read

Callers that require an exact byte count must loop until enough bytes have been
read, EOF is reached, or an error occurs.

### 8.12 `write()` semantics

```c
mini_result_t write(
    mini_file_t file,
    const void *buffer,
    uint32_t size,
    uint32_t *out_written);
```

Requirements:

- `out_written` must be non-NULL
- `buffer` must be non-NULL when `size > 0`
- the handle must have been opened with `MINI_FS_WRITE`

Behavior:

- `*out_written` is set to zero before attempting the write
- a successful write may write fewer bytes than requested
- `size == 0` returns `MINI_OK` with `*out_written == 0`
- when `size > 0`, `MINI_OK` must report `*out_written > 0`; a backend that
  cannot make progress returns an error such as `MINI_ERR_NO_SPACE` or
  `MINI_ERR_IO`
- on success, the file position advances consistently with the bytes written
- with `MINI_FS_APPEND`, each write begins at EOF even after a `seek()`

Callers that require all bytes to be written must loop until complete or until
an error occurs.

Filesystem ABI v0 does not provide `write_all()`.

### 8.13 `seek()` semantics

```c
mini_result_t seek(
    mini_file_t file,
    int64_t offset,
    uint32_t origin,
    uint64_t *out_position);
```

Requirements:

- `out_position` must be non-NULL
- `origin` must be one of the values in section 8.6

On success:

- returns `MINI_OK`
- places the resulting absolute byte position in `*out_position`
- subsequent reads and non-append writes use that position

`*out_position` is set to zero before attempting the seek. A failed seek does not
promise a new file position; callers should treat the current position as
unchanged unless a future ABI explicitly says otherwise.

### 8.14 `sync()` semantics

```c
mini_result_t sync(mini_file_t file);
```

`sync()` asks MiniShell to flush pending file data and relevant metadata through
the filesystem/backend as far as that backend can provide.

It is intended for applications such as editors and loggers that need a known
persistence point before continuing or reporting a save as successful.

`MINI_OK` means the backend accepted and completed its available synchronization
operation. It does **not** promise atomic replacement, journaling, or survival of
every possible sudden-power-loss scenario.

Filesystem ABI v0 provides file-level `sync()` only; there is no global
filesystem sync operation.

### 8.15 `stat()` semantics

```c
mini_result_t stat(
    const char *path,
    mini_fs_stat_t *out_stat);
```

Requirements:

- `path` and `out_stat` must be non-NULL
- the caller must set `out_stat->struct_size` before the call
- `struct_size` must be large enough to contain the `struct_size` field itself

On success, MiniShell fills the fields supported by the caller's structure size.
For filesystem ABI v0, a full-size structure reports at least `type` and `size`.

A missing path returns `MINI_ERR_NOT_FOUND`.

### 8.16 Resource cleanup

MiniShell remains the owner of the underlying file resources.

If an application returns while one or more `mini_file_t` handles remain open,
the app manager must reclaim those resources during normal application teardown
before unloading the ELF.

Conceptually:

```text
app returns
    |
    v
app manager
    |
    +--> close/reclaim app-owned file handles
    |
    v
unload ELF
    |
    v
M$>
```

Automatic reclamation is a safety net, not a substitute for applications calling
`close()` and checking errors normally.

### 8.17 Intentionally outside filesystem ABI v0

The following are postponed until a real application requires them:

```text
readline
write_all
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
filesystem mount / unmount
filesystem-specific control calls
```

A future robust editor-save sequence may motivate `rename()` and `remove()` so a
new file can be written and synchronized before replacing the old file. `med` v0
can save using `open(WRITE|CREATE|TRUNC)`, `write()`, `sync()`, and `close()`.

## 9. Hardware Access Levels

### Portable

Uses only standard MiniShell ABI calls.

### Platform-aware

Uses standard ABI plus documented optional platform extensions.

### Bare hardware

Directly uses MCU registers, SDK calls, or peripheral drivers.

MiniShell does not prevent bare-hardware access. If such an app changes hardware
state expected by MiniShell, it must restore that state before returning or
accept that the system may fail.

## 10. Foreground App Lifecycle

V1 supports one foreground native app at a time.

Normal lifecycle:

```text
resolve
  -> validate
  -> load
  -> bind/pass API
  -> enter application
  -> run
  -> return exit code
  -> MiniShell cleanup
  -> unload
  -> M$>
```

An application should prefer returning normally rather than resetting or powering
down the MCU.

A future explicit `request_exit()` mechanism may be useful for deep call stacks,
but normal function return remains the preferred V1 lifecycle.

## 11. Resource Ownership During an App

MiniShell remains the owner of system services while an app is active.

An app may acquire logical resources such as:

- open files
- display foreground ownership
- audio stream/session
- input subscription
- timers
- network handles

MiniShell tracks resources created through its own APIs so they can be released
during normal app teardown. Filesystem ABI v0 is the first service to make this
rule concrete through application-owned `mini_file_t` handles backed by
MiniShell-owned resources.

This bookkeeping is cooperative cleanup, not protection from arbitrary memory
corruption.

## 12. Foreground Display/Input Model

A foreground application may temporarily control the user-facing display and
input through MiniShell services, but MiniShell remains the hardware owner.

Conceptually:

```text
shell owns foreground
    |
launch app
    v
app owns foreground session through API
    |
app exits
    v
MiniShell restores shell foreground
```

This allows a full-screen application such as `med` or MiniFT8 without requiring
the app to initialize the display, terminal transport, keyboard, or other input
hardware.

## 13. Direct Access Escape Hatch

MiniShell intentionally does not forbid direct hardware access.

A developer may choose it when:

- the standard API cannot express a timing-critical operation
- a specialized peripheral is not yet represented
- experimentation requires direct register access
- performance justifies bypassing a service layer

That application becomes platform-specific and assumes responsibility for
preserving or restoring system state.

The existence of the escape hatch must not be used as an excuse to let ordinary
apps casually bypass the service architecture.

## 14. Non-Goals for ABI V1

ABI V1 does not need:

- POSIX compatibility
- `fork()` / `exec()` semantics
- processes
- users or permissions
- virtual memory
- memory protection
- arbitrary shared-library compatibility
- background applications
- full FreeRTOS API exposure
- full ESP-IDF API exposure
- binary compatibility across CPU architectures

## 15. Task 0 ABI Validation

Task 0 validated the initial runtime ABI mechanism on real M5Stack Tab5 hardware.

`hello.elf` was:

1. built separately from MiniShell
2. stored on microSD
3. launched by typing `hello`
4. bound to the resident MiniShell API through `mini_api_get()`
5. able to call the resident system write service
6. able to return normally
7. unloaded cleanly
8. launched repeatedly while MiniShell remained usable without reboot

Task 1 now uses a real application (`med`) to validate and refine the first
substantial service APIs.
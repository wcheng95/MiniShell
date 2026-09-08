# MiniShell Filesystem API

Status: **implemented and exercised on the Linux reference backend.**

## Purpose

The Filesystem API gives applications a logical MiniShell namespace without exposing POSIX descriptors, `DIR *`, FATFS objects, mount structures, or storage-driver types.

```text
application
    |
Filesystem API
    |
portable Filesystem service
    |
private backend
    |
Linux POSIX / NuttX / FATFS / other
```

The Filesystem service owns application-visible paths, file/directory handles, namespace semantics, lifecycle cleanup, and MiniShell storage quota behavior.

## Logical namespace

Paths are absolute MiniShell paths:

```text
/sd/log.txt
/flash/config.ini
```

Rules implemented by the portable service include:

- `/` separator;
- repeated separators collapsed;
- `.` ignored;
- `..` normalized but may not escape `/`;
- no platform path syntax crosses the API;
- root is protected from destructive namespace operations.

Linux maps this namespace under a private host root (normally `~/.local/share/minishell/fs`) but applications never see that path.

## File handles

```c
typedef uint32_t mini_file_t;
#define MINI_FILE_INVALID ((mini_file_t)0u)
```

Handles are opaque and owned by the current foreground app. The portable service uses generation-aware slots so stale handles are rejected. Remaining open handles are reclaimed at app teardown.

## Directory handles

```c
typedef uint32_t mini_dir_t;
#define MINI_DIR_INVALID ((mini_dir_t)0u)
```

Directory handles follow the same ownership/lifecycle rule. Applications never receive a backend directory object.

## Open flags

```c
MINI_FS_READ
MINI_FS_WRITE
MINI_FS_CREATE
MINI_FS_EXCL
MINI_FS_TRUNC
MINI_FS_APPEND
```

Important combinations:

- at least READ or WRITE is required;
- CREATE/EXCL/TRUNC/APPEND require the combinations enforced by the service;
- unknown bits return `MINI_ERR_INVALID`;
- APPEND writes start at logical EOF.

## Core operations

```c
open
close
read
write
seek
sync
stat
```

Read/write support partial progress. EOF is `MINI_OK` plus zero bytes read. File positions and access permissions are tracked by the portable service rather than exposed as backend descriptors.

## Namespace operations

The current API includes:

```c
rename
remove_file
mkdir
rmdir
```

`rename(old, new)` is intentionally regular-file focused. If `new` names an existing regular file, that file is replaced by `old`; after success `old` no longer exists and `new` names the former source content. Replacing a directory is rejected.

The portable service also rejects a rename while either the source or destination has an active writable MiniShell handle. This preserves deterministic path ownership/accounting across backends.

The replacement is performed through one backend rename operation. Linux maps this directly to POSIX `rename()`, which provides the normal same-filesystem atomic namespace replacement. Other backends must implement the same application-visible replacement semantics; exact crash/power-loss guarantees remain filesystem/backend properties.

This contract supports the standard safe-save pattern used by applications such as MiniFT8:

```text
write Station.txt.tmp
sync + close
rename Station.txt.tmp -> Station.txt
```

Recursive deletion/copy is not a primitive.

## Directory iteration

```c
mini_result_t (*dir_open)(const char *path, mini_dir_t *out_dir);
mini_result_t (*dir_read)(mini_dir_t dir,
                          mini_fs_dir_entry_t *out_entry,
                          uint32_t *out_has_entry);
mini_result_t (*dir_close)(mini_dir_t dir);
```

Directory entry:

```c
#define MINI_FS_NAME_MAX 255u

typedef struct {
    uint32_t struct_size;
    uint32_t type;
    char name[MINI_FS_NAME_MAX + 1u];
} mini_fs_dir_entry_t;
```

`dir_read()` returns `MINI_OK` at both a normal entry and end-of-directory; `out_has_entry` distinguishes them. `.` and `..` are filtered by the portable service. Entry type is reduced to MiniShell file/directory values.

`ls` is simply one portable consumer of this API.

## Space/quota information

```c
mini_result_t (*space)(const char *path,
                       mini_fs_space_t *out_space);
```

with:

```c
total_bytes
used_bytes
free_bytes
```

These values describe **MiniShell-visible storage**, not necessarily the physical device's raw capacity.

On Linux the default MiniShell storage limit is 64 MiB. The Filesystem service enforces growth against the configured limit, and `df` reports the same enforced domain. A limit of zero means no MiniShell quota; a backend may then expose its meaningful native capacity if supported by policy.

`path` remains part of the API because platforms may expose distinct storage resources such as `/sd` and `/flash` with different capacities.

## Storage accounting policy

When a MiniShell storage quota is active, the service scans regular-file sizes under the logical namespace and checks requested file growth before writes. Namespace/directories do not consume quota bytes in the current policy; regular-file content does.

Replacing an existing destination with `rename()` removes the replaced destination's size from MiniShell storage usage; the source content was already counted before the namespace change.

To keep accounting deterministic, the current service prevents simultaneous writable handles to the same normalized logical path and blocks rename of a source/destination with an active writable handle.

## Ownership and one-owner rule

```text
application owns logical handle token
        |
Filesystem service owns handle state + path/quota semantics
        |
backend owns native operation objects
        |
OS/driver owns physical storage implementation
```

The application cannot bypass this model and remain portable.

Backend/platform errors are translated to `mini_result_t`; `errno`, FATFS values, or SDK errors do not cross the public boundary.

## API evolution

`struct_size`, opaque handles, and fixed-width public types remain useful design mechanisms. They do not imply a backward-compatibility guarantee. The Filesystem API may be reorganized or changed incompatibly while MiniShell is still being shaped; in-tree applications are rebuilt against the matching API.

## Current verification

Linux and unit tests exercise:

- file create/read/write/stat/rename/remove/mkdir/rmdir;
- rename to a new destination and replacement of an existing regular file;
- source disappearance and destination-content preservation after replacement;
- replacement storage-quota accounting;
- writable-handle protection around rename;
- path normalization and root escape protection;
- handle lifecycle/cleanup;
- directory open/read/close and file/directory classification;
- portable `ls` including root `/sd` and `/flash` presentation;
- quota enforcement and `space()`/`df` behavior.

## Deliberately deferred

No current portable requirement justifies:

```text
working directory / cd
wildcards/globbing
permissions/ownership
links
file locking
mmap
async I/O
mount/unmount API
recursive copy/remove primitive
```

Add these only when a real application needs a generally useful primitive.

## Internal housekeeping

The Filesystem service remains the single semantic owner even when its implementation is split into private path/handle/quota helpers. Internal file boundaries do not change the public ownership model.

# MiniShell Application API

Status: **current application contract; compatibility not frozen.**

## 1. Goal

The Application API lets application source remain independent of the platform. On capable targets an application may also be packaged and loaded separately, but loading format is not part of the public API.

The API is a C contract owned by MiniShell. It is not POSIX, NuttX, ESP-IDF, or a specific loader format.

## 2. Entry point

A native application currently exposes:

```c
int main(int argc, char **argv);
```

Arguments are supplied by the MiniShell command line.

## 3. Runtime API acquisition

The application obtains MiniShell services through:

```c
const mini_api_t *api = mini_api_get();
```

The returned table is owned by MiniShell and is valid for the current application execution.

`mini_api_t` currently begins with:

```text
api_version
struct_size
system
memory
filesystem
time/location
display
input
audio
```

`MINISHELL_API_VERSION` identifies the API generation expected by the current source/build. It is **not** currently a promise that an older independently compiled binary will remain compatible with a newer MiniShell.

## 4. Packaging and loader independence

The executable/container format is a backend concern:

```text
Linux/Mint      shared object loaded with dlopen()
Cardputer ADV   V1 compiled-in application registry
Tab5/NuttX      native loadable mechanism where practical
future ADV      runtime-loaded apps may be explored later
```

An application should not contain loader-specific code.

Portable source is rebuilt for the target CPU/toolchain and current MiniShell API. A formal cross-release binary ABI may be introduced later if independently built `.so` or `.elf` applications need to remain compatible with newer MiniShell releases.

## 5. Service discovery

Applications check only the services, fields, capabilities, and optional operations they require. `struct_size` and capability bits remain useful for defensive discovery and optional features.

These mechanisms do **not** currently imply append-only growth or backward compatibility. During active architectural development MiniShell may change table layout, names, function signatures, or semantics when that produces a cleaner API.

### Filesystem directory iteration

The Filesystem service provides opaque directory handles and MiniShell-owned directory-entry records:

```c
mini_result_t dir_open(const char *path, mini_dir_t *out_dir);
mini_result_t dir_read(mini_dir_t dir,
                       mini_fs_dir_entry_t *out_entry,
                       uint32_t *out_has_entry);
mini_result_t dir_close(mini_dir_t dir);
```

`dir_read()` returns `MINI_OK` for both a normal entry and end-of-directory. `out_has_entry` distinguishes them. `.` and `..` are filtered by the portable Filesystem service. Platform types such as POSIX `DIR *` and `struct dirent` never cross the API.

Open directory handles are application-owned resources and are closed automatically when the foreground application exits, like MiniShell file handles and managed memory.

### Filesystem space

```c
mini_result_t space(const char *path, mini_fs_space_t *out_space);
```

The result contains MiniShell-visible `total_bytes`, `used_bytes`, and `free_bytes`. These describe the storage resource MiniShell permits applications to use, not necessarily raw host/device capacity.

### Resource policy

Resource limits are resident MiniShell policy rather than a separate application service. Applications observe them through normal service behavior:

```text
Memory alloc/realloc beyond budget    -> MINI_ERR_NO_MEMORY
Filesystem growth beyond budget       -> MINI_ERR_NO_SPACE
Memory get_info                        -> MiniShell-visible usage/free values
Filesystem space                       -> MiniShell-visible used/free/total
```

### UTC setting

`MINI_TIMELOC_CAP_SET_UTC` means MiniShell UTC can be re-anchored with `utc_set()`. Whether the operation is durable is backend policy. Linux keeps the correction for the current MiniShell session; a backend owning a writable RTC may persist it.

### Text attributes

The Text Display API may provide `write_at_attr()`. `MINI_TEXT_ATTR_INVERSE` is the first defined attribute. A backend that does not support it may leave the pointer NULL; applications should provide a fallback when the attribute is optional.

## 6. Compatibility policy

MiniShell is early enough that the public API is deliberately **not frozen**:

- backward source compatibility is not guaranteed;
- backward binary compatibility is not guaranteed;
- in-tree applications are rebuilt when the API changes;
- API changes should be deliberate, documented, and covered by tests;
- a stable binary ABI is deferred until real separately distributed applications justify it.

Compatibility should be frozen deliberately, not accidentally.

## 7. Lifecycle

V1 supports one foreground application:

```text
resolve
  -> load/prepare or select compiled-in app
  -> app_begin
  -> call main(argc, argv)
  -> use MiniShell API
  -> return
  -> reclaim MiniShell-managed resources
  -> unload/release where applicable
  -> app_end
  -> restore shell
```

Returning from `main()` is the normal application exit path.

## 8. No platform leakage

Portable application headers must not require:

```text
POSIX file descriptors
POSIX DIR / dirent types
NuttX driver types
ESP-IDF types
FreeRTOS handles
board-driver objects
```

MiniShell-owned platform-neutral types and opaque handles remain the public boundary.

## 9. Runtime installation goal

Runtime installation remains desirable on platforms where it is practical, but it is not required by the API itself.

The user model remains:

```text
M$> apps
MiniFT8
MiniCW
MiniRTTY

M$> run MiniFT8
...
M$>
```

Direct application invocation is equivalent:

```text
M$> MiniFT8
```

Linux currently provides dynamic `.so` loading. Cardputer ADV V1 will provide the same foreground lifecycle through a compiled-in registry. Runtime `.elf` loading on ADV is deferred for later investigation rather than rejected.

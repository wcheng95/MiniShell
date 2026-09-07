# MiniShell Application ABI

Status: provisional ABI generation 1.

## 1. Goal

The Application ABI lets an application core remain independent of the platform and, on capable targets, be installed and run without rebuilding MiniShell.

The ABI is a C contract owned by MiniShell. It is not POSIX, NuttX, ESP-IDF, or a specific loader format.

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

## 4. Loader independence

The executable/container format is a backend concern:

```text
Linux/Mint      shared object loaded with dlopen()
Tab5/NuttX      native loadable application mechanism where practical
ADV             compiled-in application may be used when RAM makes loading impractical
```

An application should not contain loader-specific code.

Portable source is expected to be rebuilt for the CPU/machine ABI of each target. Binary compatibility across incompatible architectures is not a goal.

## 5. Service table

`mini_api_t` begins with:

```text
abi_version
struct_size
system
memory
filesystem
time/location
display
input
```

System is the minimum service required by the current reference `hello` app. Optional services may be NULL on a platform that does not provide them.

New service pointers and service functions are appended under the existing `struct_size` compatibility rules.

### Filesystem directory iteration

The Filesystem service provides opaque directory handles and MiniShell-owned directory-entry records:

```c
mini_result_t dir_open(const char *path, mini_dir_t *out_dir);
mini_result_t dir_read(mini_dir_t dir,
                       mini_fs_dir_entry_t *out_entry,
                       uint32_t *out_has_entry);
mini_result_t dir_close(mini_dir_t dir);
```

`dir_read()` returns `MINI_OK` for both a normal entry and end-of-directory. `out_has_entry` distinguishes them:

```text
1   out_entry contains one file or directory
0   end of directory
```

`.` and `..` are filtered by the portable Filesystem service. Unsupported native entry types are skipped. Applications receive only MiniShell file/directory type values and a fixed-size name field; platform types such as POSIX `DIR *` and `struct dirent` never cross the ABI.

Open directory handles are tracked as application-owned resources and are closed automatically when the foreground application exits, just like MiniShell file handles and managed memory.

This primitive exists for general application needs such as MiniFT8 log discovery. `ls` is simply one application built on the same API.

### Filesystem space

Filesystem ABI v1 also appends:

```c
mini_result_t space(const char *path, mini_fs_space_t *out_space);
```

The result contains MiniShell-visible `total_bytes`, `used_bytes`, and `free_bytes`.
These values describe the storage resource MiniShell permits applications to use;
they are not required to expose the raw capacity of an underlying host disk.
`df` is one consumer, but applications may use the same primitive before creating
or expanding files.

### Resource policy

Resource limits are resident MiniShell policy and are not themselves an
application service. Applications observe the policy through normal service
behavior:

```text
Memory alloc/realloc beyond budget    -> MINI_ERR_NO_MEMORY
Filesystem growth beyond budget       -> MINI_ERR_NO_SPACE
Memory get_info                        -> MiniShell-visible usage/free values
Filesystem space                       -> MiniShell-visible used/free/total
```

A host may deliberately constrain MiniShell to embedded-scale resources. A target
with naturally limited resources may map the same semantics to its real memory and
storage constraints.

### UTC setting

`MINI_TIMELOC_CAP_SET_UTC` means MiniShell UTC can be re-anchored with `utc_set()`.
Whether that operation is durable is backend policy. Linux keeps the correction
for the current MiniShell session only; a backend that owns a writable RTC may
store the same setting in hardware.

### Text attributes

The Text Display table may append `write_at_attr()`. `MINI_TEXT_ATTR_INVERSE` is
the first defined attribute. A backend that does not support text attributes may
leave this function NULL; applications should provide a simple fallback when the
attribute is optional. Linux maps inverse text to terminal reverse video, while a
framebuffer backend can implement the same semantic effect by swapping foreground
and background colors.

## 6. Compatibility

Applications check the ABI generation and only the fields/capabilities they actually require. Compatible append-only expansion does not require an ABI generation bump.

An incompatible layout or semantic change requires a new ABI generation.

## 7. Lifecycle

V1 supports one foreground application:

```text
resolve
  -> load/prepare
  -> call main(argc, argv)
  -> use MiniShell services
  -> return
  -> reclaim MiniShell-managed resources
  -> unload/release
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

MiniShell-owned fixed-width types and opaque handles remain the public boundary.

## 9. Runtime installation goal

On platforms with practical dynamic loading, copying a compatible app into the MiniShell app directory should be sufficient for discovery and execution without rebuilding MiniShell.

The reference user model is:

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

Constrained platforms may implement the same lifecycle with a compiled-in registry if runtime loading is not economical.

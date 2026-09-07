# MiniShell

MiniShell is a platform-adaptive application runtime. Its job is to keep application cores independent of Linux, NuttX, ESP-IDF, board drivers, and other platform details while providing a small common shell, application lifecycle, and service ABI.

Linux Mint on `pc-1` is the reference implementation and a full production target.

## Core model

```text
Applications
    MiniFT8 / MiniCW / MiniRTTY / tools
                    |
              MiniShell ABI
                    |
        +-----------+-----------+
        |      MiniShell Core    |
        | shell / app lifecycle  |
        | portable service ABI   |
        | resource policy        |
        +-----------+-----------+
                    |
           private backend API
        +-----------+-----------+
        |           |           |
      Linux       NuttX      embedded
      POSIX        Tab5       thick ADV
```

Applications use MiniShell services only. Mocks and simulated devices also live below MiniShell; they do not connect directly to application cores.

## Shell baseline

The resident Linux shell is intentionally small:

```text
help
status
apps
run <app> [...]
<app> [...]
exit
```

`status` reports platform identity and MiniShell service availability. `run <app>` and direct `<app>` use the same internal application-launch path.

There is no separate user-facing `exec` command in the Linux baseline. `put/get`, `suspend`, and `poweroff` are platform-dependent operations and are not faked on Mint merely to make command sets identical.

## Application model

MiniShell keeps one foreground application active at a time. On platforms that support runtime loading, applications can be installed and run without rebuilding MiniShell.

Linux uses shared objects and `dlopen()` as its loader implementation. The loader mechanism is private; application source depends only on the MiniShell C ABI.

```text
M$> apps
cat
cp
date
df
free
hello
ls
mkdir
mv
nano
rm
rmdir

M$> ls /sd
notes.txt
logs/

M$> free
used 0B  free 8.0M  total 8.0M

M$> df
used 0B  free 64.0M  total 64.0M

M$> date
2026-09-07 00:10:00 UTC
```

A native application currently exposes:

```c
int main(int argc, char **argv);
```

and obtains services with:

```c
const mini_api_t *api = mini_api_get();
```

## Build on Linux Mint

Requirements: CMake, a C compiler, Python 3, and the normal POSIX dynamic-loader library.

Use a dedicated Linux build directory so an old ESP-IDF or other cross-toolchain CMake cache cannot be reused accidentally:

```bash
cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"
ctest --test-dir build-linux --output-on-failure
./build-linux/minishell
```

Runtime applications are built into:

```text
build-linux/runtime/apps/
```

The Linux backend discovers `.so` applications in that directory. Set `MINISHELL_APP_DIR` to override it.

## Resource policy

MiniShell deliberately reports and enforces the resources available through MiniShell rather than exposing the much larger host machine directly.

Linux defaults are:

```text
memory   8 MiB
storage 64 MiB
```

Override them for experiments with:

```bash
MINISHELL_MEMORY_LIMIT=16M MINISHELL_STORAGE_LIMIT=128M ./build-linux/minishell
```

Accepted suffixes are `K`, `M`, and `G`; `0` means unlimited.

The Memory service rejects MiniShell allocations beyond the memory budget. The Filesystem service accounts regular files under the logical MiniShell root and rejects writes that would exceed the storage budget. `free` and `df` report those same enforced budgets.

These limits apply to resources acquired through the MiniShell ABI. Portable applications should therefore use MiniShell Memory and Filesystem services rather than bypassing them with host APIs.

## Current portable applications

The same application source is used through the MiniShell ABI rather than through Linux/POSIX APIs:

```text
hello    minimal ABI example
cat      display a text file
cp       binary-safe file copy
date     show/set MiniShell UTC
df       MiniShell storage used/free/total
free     MiniShell memory used/free/total
ls       enumerate a directory
mv       no-overwrite regular-file rename
rm       remove one regular file
mkdir    create one directory
rmdir    remove one empty directory
nano     small interactive text editor
```

`nano` uses MiniShell Memory, Filesystem, Display, and Input services. Its basic controls are:

```text
Ctrl-O   save
Ctrl-W   search
Ctrl-X   exit
```

## Time model

On Linux, MiniShell reads system UTC at startup and anchors it to `CLOCK_MONOTONIC`.

```text
MiniShell UTC = startup UTC anchor + monotonic elapsed
```

`date YYYY-MM-DD HH:MM:SS` re-anchors MiniShell UTC for the current session. It does not change Linux system time and the correction is not persisted across MiniShell restarts. A backend that owns a writable RTC may persist the same Time/Location ABI set operation.

## Public ABI

The public header is:

```text
include/minishell/api.h
```

ABI generation 1 currently provides these established service groups on Linux:

```text
System
Memory
Filesystem
Time/Location
Display
Input
```

The Filesystem ABI supports regular-file operations, namespace operations, directory iteration, and storage-space reporting:

```text
dir_open(path)
dir_read(handle)
dir_close(handle)
space(path)
```

Directory handles are opaque MiniShell handles. Applications receive MiniShell-owned entry types and names, never POSIX `DIR *` or `struct dirent`. Open file and directory handles are reclaimed automatically at application exit.

The directory and space APIs are application functionality, not command-specific special cases. MiniFT8 and other applications can use them to discover logs and to reason about available MiniShell storage.

The portable service core is shared with other MiniShell backends. Linux supplies POSIX implementations underneath it; applications never receive POSIX file descriptors, terminal objects, directory objects, or other host-specific types.

## Linux service mapping

```text
MiniShell service      Linux reference backend
-------------------------------------------------------------
System                 stdout terminal output
Memory                 malloc/realloc/free beneath MiniShell quota
Filesystem             POSIX files below logical root + MiniShell quota
Time/Location          CLOCK_MONOTONIC + startup system UTC anchor
Display                ANSI terminal text display
Input                  terminal key events through poll/read
```

The default logical filesystem root is:

```text
~/.local/share/minishell/fs/
    sd/
    flash/
```

An application path such as `/sd/log.txt` remains a MiniShell path rather than a Linux pathname. Set `MINISHELL_ROOT` to override the host directory used as MiniShell `/`.

## Platform policy

MiniShell may be thin or thick depending on the target:

- **Linux/Mint:** thin POSIX backend; runtime-loaded external apps.
- **Tab5/ESP32-P4:** thin MiniShell layer over NuttX where practical; loadable apps are preferred.
- **Cardputer ADV:** thick MiniShell backend is preferred because RAM is scarce. Apps may be compiled together if runtime loading is too expensive.

The user-facing application model should remain consistent where that is useful, but platform-dependent operations are allowed to remain platform-dependent rather than being represented by fake implementations.

## Current Linux baseline

Automated tests cover:

- application discovery and explicit/direct launch;
- resident `status` behavior;
- Memory allocation/reallocation/free, accounting, and quota enforcement;
- logical Filesystem create/read/write/stat/rename/remove/mkdir/rmdir;
- Filesystem directory open/read/close and file/directory classification;
- Filesystem storage accounting and quota enforcement;
- `free`, `df`, and `date` through the public ABI;
- session-only `date` correction on Linux and reload from system UTC after restart;
- monotonic time, sleep, system UTC, and persistent default-location operations;
- terminal Display and real Input handoff through a pseudo-terminal;
- `cat`, `cp`, `ls`, `mkdir`, `mv`, `rm`, and `rmdir` as runtime-loaded portable apps;
- a real `nano` edit/save/exit session through a pseudo-terminal;
- automatic app-resource cleanup and clean return to `M$>`.

## Next direction

The generic Linux MiniShell baseline is now complete. The next phase should be driven top-down by MiniFT8-V3 requirements. New service groups such as Audio or Radio/CAT should be added only when the application boundary has been defined and a real MiniFT8 vertical slice requires them.

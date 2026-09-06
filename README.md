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

There is no separate user-facing `exec` command in the Linux baseline. `put/get` are also omitted on Linux; platforms that need a serial, USB, BLE, or other provisioning/recovery transfer channel may add them later.

## Application model

MiniShell keeps one foreground application active at a time. On platforms that support runtime loading, applications can be installed and run without rebuilding MiniShell.

Linux uses shared objects and `dlopen()` as its loader implementation. The loader mechanism is private; application source depends only on the MiniShell C ABI.

```text
M$> apps
cat
cp
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

M$> nano /sd/notes.txt
...

M$>
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

## Current portable applications

The same application source is used through the MiniShell ABI rather than through Linux/POSIX APIs:

```text
hello    minimal ABI example
cat      display a text file
cp       binary-safe file copy
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

The Filesystem ABI supports regular-file operations, namespace operations, and directory iteration:

```text
dir_open(path)
dir_read(handle)
dir_close(handle)
```

Directory handles are opaque MiniShell handles. Applications receive MiniShell-owned entry types and names, never POSIX `DIR *` or `struct dirent`. Open file and directory handles are reclaimed automatically at application exit.

This directory API is application functionality, not an `ls` special case. MiniFT8 and other applications can use the same primitives to discover logs, configuration files, or other directory contents.

The portable service core is shared with other MiniShell backends. Linux supplies POSIX implementations underneath it; applications never receive POSIX file descriptors, terminal objects, directory objects, or other host-specific types.

## Linux service mapping

```text
MiniShell service      Linux reference backend
-------------------------------------------------------------
System                 stdout terminal output
Memory                 malloc/realloc/free + app accounting
Filesystem             POSIX files/directories below logical root
Time/Location          CLOCK_MONOTONIC, system UTC, default location
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

The user-facing application model should remain consistent even when the implementation differs.

## Current Linux baseline

Automated tests cover:

- application discovery and explicit/direct launch;
- resident `status` behavior;
- Memory allocation/reallocation/free and per-app accounting;
- logical Filesystem create/read/write/stat/rename/remove/mkdir/rmdir;
- Filesystem directory open/read/close and file/directory classification;
- `ls` using only the public Filesystem ABI;
- monotonic time, sleep, system UTC, and persistent default-location operations;
- terminal Display and real Input handoff through a pseudo-terminal;
- `cat`, `cp`, `ls`, `mkdir`, `mv`, `rm`, and `rmdir` as runtime-loaded portable apps;
- a real `nano` edit/save/exit session through a pseudo-terminal;
- automatic app-resource cleanup and clean return to `M$>`.

## Next direction

Use this Linux baseline as the application-development reference. New MiniShell service groups are added only when a real application requires them. MiniFT8-V3 will consume live QMX audio/CAT through future MiniShell services; file and simulated providers will live underneath those same MiniShell boundaries.

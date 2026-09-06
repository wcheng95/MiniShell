# MiniShell

MiniShell is a platform-adaptive application runtime. Its job is to keep application cores independent of Linux, NuttX, ESP-IDF, board drivers, and other platform details while still providing a small common shell, application lifecycle, and service ABI.

Linux Mint on `pc-1` is the reference implementation and a full production target, not a simulator or pre-port environment.

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

Applications use MiniShell services only. Host mocks and simulated devices also live below MiniShell; they do not connect directly to application cores.

## Application model

MiniShell keeps one foreground application active at a time. On platforms that support runtime loading, applications can be installed and run without rebuilding MiniShell.

Linux uses shared objects and `dlopen()` for the reference runtime-loader implementation. The application-facing contract remains the MiniShell C ABI rather than the Linux loader mechanism.

```text
M$> apps
hello
M$> run hello
Hello from MiniShell.
M$> hello
Hello from MiniShell.
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

The `hello` module is built separately as:

```text
build-linux/runtime/apps/hello.so
```

The Linux backend discovers applications next to the MiniShell executable under `runtime/apps`. Set `MINISHELL_APP_DIR` to override that location.

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

The portable service core is shared with other MiniShell backends. Linux supplies POSIX implementations underneath it; applications never receive POSIX file descriptors, terminal objects, or other host-specific types.

The ABI remains MiniShell-owned: portable applications must not depend on POSIX, NuttX, ESP-IDF, FreeRTOS, or board-specific types.

## Linux service mapping

```text
MiniShell service      Linux reference backend
-------------------------------------------------------------
System                 stdout terminal output
Memory                 malloc/realloc/free + app accounting
Filesystem             POSIX files below MiniShell logical root
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

so an application path such as `/sd/log.txt` remains a MiniShell path rather than a Linux pathname. Set `MINISHELL_ROOT` to override the host directory used as MiniShell `/`.

The Linux backend keeps default-location state under its private `.state` directory. Setting the system UTC clock is intentionally not exposed merely because Linux can do it with sufficient privilege; the Time/Location ABI reports the capabilities the backend can safely provide.

## Platform policy

MiniShell may be thin or thick depending on the target:

- **Linux/Mint:** thin POSIX backend; runtime-loaded external apps.
- **Tab5/ESP32-P4:** thin MiniShell layer over NuttX where practical; loadable apps are preferred.
- **Cardputer ADV:** thick MiniShell backend is preferred because RAM is scarce. Apps may be compiled together if runtime loading is too expensive.

The user-facing application model should remain consistent even when the implementation differs.

## Current baseline

The Linux reference baseline proves:

```text
start MiniShell
    -> M$> prompt
    -> discover/load/unload hello.so
    -> application calls mini_api_get()
    -> System / Memory / Filesystem / Time-Location / Display / Input
    -> automatic app-resource cleanup
    -> return to M$>
```

Automated tests cover:

- application discovery and explicit/direct launch;
- Memory allocation/reallocation/free and per-app accounting;
- logical Filesystem create/read/write/stat/rename/remove/mkdir/rmdir;
- monotonic time, sleep, system UTC, and persistent default-location operations;
- terminal Display geometry/write/clear/present;
- real Input handoff through a pseudo-terminal to a separately loaded app;
- clean return from the app to `M$>`.

## Next direction

Use this Linux baseline to run the existing portable MiniShell applications natively, then grow new service groups only when real applications require them. MiniFT8-V3 will consume live QMX audio/CAT through future MiniShell services; file and simulated providers will live underneath those same MiniShell boundaries.

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

The public header remains:

```text
include/minishell/api.h
```

ABI generation 1 defines the established System, Memory, Filesystem, Time/Location, Display, and Input service shapes. The first Linux baseline activates the System service required by `hello`; the remaining service backends will be ported behind the same ABI as application requirements pull them in.

The ABI remains MiniShell-owned: portable applications must not depend on POSIX, NuttX, ESP-IDF, FreeRTOS, or board-specific types.

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
    -> discover hello.so
    -> load without rebuilding MiniShell
    -> hello calls mini_api_get()
    -> hello calls System.write
    -> return to M$>
    -> unload
```

`tests/linux_smoke.py` validates discovery, explicit `run hello`, direct `hello`, return to the shell, and clean process exit.

## Next direction

Port the remaining established service ABIs to the Linux backend, then bring MiniFT8-V3 in as a real MiniShell application. QMX live audio/CAT will enter through MiniShell services; file and simulated backends will implement those same services underneath MiniShell.

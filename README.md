# MiniShell

MiniShell is a platform-adaptive application runtime. It keeps application cores independent of Linux, NuttX, ESP-IDF, board drivers, and mocks while providing a small shell, application lifecycle, resource policy, and stable service ABI.

Linux Mint on `pc-1` is the reference implementation and a full production target.

## Core model

```text
Applications
  MiniFT8 / MiniCW / MiniRTTY / tools
                    |
              MiniShell ABI
                    |
        portable MiniShell core
  shell / lifecycle / services / policy
                    |
           private backend API
        +-----------+-----------+
        |           |           |
      Linux       NuttX      embedded
      POSIX        Tab5      ESP32-S3
```

Applications use MiniShell services only. Mocks/simulated providers also live below MiniShell.

## Linux shell baseline

Resident commands are intentionally small:

```text
help
status
apps
run <app> [...]
<app> [...]
exit
```

Current portable applications include the small utilities plus the first substantial domain application:

```text
MiniFT8
hello
cat
cp
date
df
free
ls
mkdir
mv
nano
rm
rmdir
```

Example:

```text
M$> ls
/sd
/flash

M$> MiniFT8
... 30x8 MiniFT8 UI ...
q
M$>
```

## Application model

A native application currently exposes:

```c
int main(int argc, char **argv);
```

and acquires services through:

```c
const mini_api_t *api = mini_api_get();
```

Physical packaging/loading is backend-private:

```text
Linux/Mint      .so + dlopen/dlsym/dlclose
Tab5/NuttX      loadable-app mechanism where practical
Cardputer ADV   compiled-in registry acceptable when loading is too expensive
```

The user model remains `apps`, `run <app>`, direct `<app>`, application return, then `M$>`.

## Build on Linux Mint

```bash
cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"
ctest --test-dir build-linux --output-on-failure
./build-linux/minishell
```

Use a dedicated Linux build directory so an old ESP-IDF/cross-toolchain CMake cache cannot be reused accidentally.

Runtime Linux modules are built under:

```text
build-linux/runtime/apps/
```

`MINISHELL_APP_DIR` can override the discovery directory.

## Resource policy

Linux deliberately constrains the MiniShell application environment instead of reporting the host PC's huge resources.

Defaults:

```text
memory   8 MiB
storage 64 MiB
```

Override for experiments:

```bash
MINISHELL_MEMORY_LIMIT=16M \
MINISHELL_STORAGE_LIMIT=128M \
./build-linux/minishell
```

The Memory and Filesystem services enforce these budgets. `free` and `df` report the same resource domains applications can actually use.

## Time model

On Linux, MiniShell reads system UTC at startup and anchors it to monotonic time.

```text
MiniShell UTC = startup UTC anchor + monotonic elapsed
```

`date YYYY-MM-DD HH:MM:SS` re-anchors MiniShell UTC for the current session only. It does not change Linux system time and does not persist an offset. A backend owning a writable RTC may persist the equivalent `utc_set()` operation.

## Public ABI

Source of truth:

```text
include/minishell/api.h
```

ABI generation 1 currently exposes:

```text
System
Memory
Filesystem
Time/Location
Display
Input
```

Notable application-driven behavior/extensions include:

```text
Filesystem     dir_open / dir_read / dir_close
Filesystem     space(path)
Filesystem     rename replaces an existing regular-file destination
Display text   optional write_at_attr(..., MINI_TEXT_ATTR_INVERSE)
```

## Ownership model

```text
app lifecycle       app_manager
app allocations     Memory service
files/dirs/quota    Filesystem service
UTC/location        Time/Location service
logical display     Display service
logical key queue   Input service
```

Backends provide primitives; portable services own application-visible semantics and lifecycle.

## MiniFT8

MiniFT8-V3 is now developed directly as a MiniShell application. Its first integrated slice uses only existing Display/Input plus Filesystem services:

```text
M$> MiniFT8
    -> 30x8 UI
    -> config/scheduler settings
    -> /flash/MiniFT8/Station.txt
    -> q
M$>
```

See `docs/MiniFT8/README.md`.

## Linux tests

The reference CTest suite now has 9 tests:

1. shell/application loading;
2. service semantics/lifecycle;
3. terminal Input;
4. portable utilities, including replacement `mv`;
5. nano PTY edit/save/exit and inverse cursor;
6. directory iteration/root `ls` behavior;
7. resource quota plus `free`/`df`/`date` and rename-accounting behavior;
8. pure MiniFT8 UI state/action smoke test;
9. MiniFT8 PTY launch/navigation/persistence/relaunch/exit integration.

CI also runs the retained platform-neutral service/unit suite.

## Documentation

Start with `docs/README.md`:

```text
docs/
├── architecture/   MiniShell system model, ownership, design rules
├── abi/            MiniShell public application contracts
├── apps/           small/medium application docs
├── MiniFT8/        MiniFT8 application architecture/UI/development
└── project/        roadmap, audit/debt, progress log
```

## Current status

The generic Linux MiniShell baseline is complete. MiniFT8-V3 is now the first substantial domain application developed against that baseline, and it has already supplied one concrete Filesystem semantic requirement: replacement `rename()` for safe saves.

The next major service work should be driven by MiniFT8's live/replayed RX vertical slice, beginning with a deliberately designed Audio ABI.

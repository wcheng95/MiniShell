# MiniShell

MiniShell is a platform-adaptive application runtime. It keeps application cores independent of Linux, NuttX, ESP-IDF, board drivers, and mocks while providing a small shell, application lifecycle, resource policy, and stable service ABI.

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

Applications use MiniShell services only. Mocks and simulated providers also live below MiniShell.

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

Current portable applications include:

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

Runtime Linux modules are built under:

```text
build-linux/runtime/apps/
```

## Resource policy

Linux deliberately constrains the MiniShell application environment instead of reporting the host PC's unconstrained resources.

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
Audio
```

Notable application-driven behavior/extensions include:

```text
Filesystem     dir_open / dir_read / dir_close
Filesystem     space(path)
Filesystem     rename replaces an existing regular-file destination
Display text   optional write_at_attr(..., MINI_TEXT_ATTR_INVERSE)
Audio          independent format-described RX/TX streams
```

Audio transports ordered frames. MiniShell does not assign application semantics such as stereo versus I/Q to channel 0/1.

## Ownership model

```text
app lifecycle       app_manager
app allocations     Memory service
files/dirs/quota    Filesystem service
UTC/location        Time/Location service
logical display     Display service
logical key queue   Input service
audio streams       Audio service
```

Backends/providers provide primitives; portable services own application-visible semantics and lifecycle.

## MiniFT8

MiniFT8-V3 is developed directly as a MiniShell application. The current application wiring includes:

```text
M$> MiniFT8
    -> 30x8 UI
    -> config/scheduler settings
    -> /flash/MiniFT8/Station.txt
    -> q
M$>
```

MiniShell Audio V1 and the deterministic Linux WAV RX provider are established. MiniFT8 treats RX Audio, TX Audio, and Control as independent logical resources. The next integration boundary is:

```text
tests/kfs16b12k.wav
    -> MiniShell Audio
    -> MiniFT8 source/profile interpretation
    -> ordinary-audio downmix
    -> ft8_engine
```

See `docs/MiniFT8/README.md`.

## Linux tests

The root Linux CTest suite currently has 11 tests covering:

1. shell/application loading;
2. service semantics/lifecycle;
3. terminal Input through a PTY;
4. portable utilities, including replacement `mv`;
5. nano PTY edit/save/exit and inverse cursor;
6. directory iteration/root `ls` behavior;
7. resource quota plus `free`/`df`/`date` and rename accounting;
8. Audio/WAV RX transport;
9. pure MiniFT8 UI state/action smoke testing;
10. stateful Linux terminal parser split-boundary behavior;
11. MiniFT8 PTY launch/navigation/persistence/relaunch/exit integration.

CI also runs the platform-neutral service/unit suite, including Audio.

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

The generic Linux MiniShell baseline is complete. Audio V1 and deterministic WAV RX are implemented, and the H1-H5 architecture-audit debt has been paid. No known MiniShell housekeeping issue blocks the deterministic MiniFT8 Audio -> DSP integration slice.

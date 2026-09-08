# MiniShell

MiniShell is a platform-adaptive application runtime. It keeps application cores independent of Linux, NuttX, ESP-IDF, board drivers, and mocks while providing a small shell, application lifecycle, resource policy, and public service API.

## Core model

```text
Applications
  FT8 / FT4 / CW / RTTY / JS8 / tools
                    |
              MiniShell API
                    |
        portable MiniShell core
  shell / lifecycle / services / policy
                    |
       private backend interface
        +-----------+-----------+
        |           |           |
      Linux       NuttX      embedded
      POSIX        Tab5      ESP32-S3
```

Applications use MiniShell services only. Mocks and simulated providers also live below MiniShell.

## Naming rule

Project/product names remain **MiniShell** and **MiniFT8** in normal prose. Code-facing names use lowercase/snake_case. Runtime protocol applications use short lowercase names:

```text
minishell
ft8
apps/ft8/
/flash/ft8/station.txt
```

Normal C conventions still apply, so preprocessor macros remain uppercase, for example `MINISHELL_API_VERSION` and `FT8_DATA_DIR`.

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
ft8
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

M$> ft8
... MiniFT8 UI ...
q
M$>
```

Future protocol applications will use similarly short names such as `ft4`, `cw`, `rtty`, and `js8`. They are separate applications rather than modes inside `ft8`.

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
Cardputer ADV   V1 compiled-in application registry
Tab5/NuttX      loadable-app mechanism where practical
```

Runtime `.elf` loading on ADV is deferred for later investigation, not rejected.

The user model remains `apps`, `run <app>`, direct `<app>`, application return, then `M$>`.

The resident shell itself uses a private platform console boundary. Linux implements that boundary with stdin/stdout; applications do **not** use it and continue through the public MiniShell APIs.

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

## Public API

Source of truth:

```text
include/minishell/api.h
```

The current API exposes:

```text
System
Memory
Filesystem
Time/Location
Display
Input
Audio
```

`MINISHELL_API_VERSION` identifies the API generation used by the current build. MiniShell is still in active architectural development, so backward source and binary compatibility are not yet promised. In-tree applications are rebuilt when the API changes. A formal binary ABI may be introduced later if separately built `.so` or `.elf` applications need compatibility across MiniShell releases.

Notable application-driven behavior includes:

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

## MiniFT8 / `ft8`

MiniFT8-V3 is the project/application design; its MiniShell runtime application is `ft8`. The runtime app is FT8-only. FT4, CW, RTTY, JS8, and other protocols will become separate applications when implemented.

MiniFT8 profiles remain application policy and independent of the MiniShell backend.

Current validation direction:

```text
Linux backend + DESKTOP profile
Linux backend + ADV profile
ADV backend   + ADV profile
```

The Linux + ADV-profile versus ADV + ADV-profile comparison is the main cross-platform architectural check. MiniFT8 RX-1B is paused until this checkpoint is complete.

See `docs/MiniFT8/README.md` and `docs/project/adv-backend-plan.md`.

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
9. pure FT8 UI state/action smoke testing;
10. stateful Linux terminal parser split-boundary behavior;
11. `ft8` PTY launch/navigation/persistence/relaunch/exit integration.

CI also runs the platform-neutral service/unit suite, including Audio.

## Documentation

Start with `docs/README.md`:

```text
docs/
├── architecture/   MiniShell system model, ownership, design rules
├── api/            MiniShell public application contracts
├── apps/           small/medium application docs
├── MiniFT8/        MiniFT8 application architecture/UI/development
└── project/        roadmap, audit/debt, progress log
```

## Current status

Stage **A0 is complete**. Portable startup now lives in `core/minishell_runtime.c`, Linux owns its C entry point and stdio console under `platform/linux/`, and the full Linux test suite remains green.

The next stage is **A1**: create the Cardputer ADV ESP-IDF build skeleton, implement the private resident console on Cardputer display/keyboard, establish ADV resource/platform identity, and prove the compiled-in app registry with a tiny app before bringing `ft8` across.

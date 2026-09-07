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
      POSIX        Tab5       thick ADV
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

`put/get`, `suspend`, `poweroff`, and similar functions are platform-dependent. Mint does not provide fake versions merely for command-set parity.

Current portable applications:

```text
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

M$> ls /sd
notes.txt
logs/

M$> free
used 0B  free 8.0M  total 8.0M

M$> df
used 0B  free 64.0M  total 64.0M
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

Suffixes `K`, `M`, `G` are accepted; `0` means unlimited.

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

Notable application-driven extensions already implemented:

```text
Filesystem     dir_open / dir_read / dir_close
Filesystem     space(path)
Display text   optional write_at_attr(..., MINI_TEXT_ATTR_INVERSE)
```

`ls`, `df`, and nano's inverse cursor are ordinary consumers of these reusable primitives, not privileged shell special cases.

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

## Nano

`nano` is intentionally split into understandable modules for orchestration, buffer editing, file persistence, UI, and utilities. It uses only MiniShell services.

Basic controls:

```text
Ctrl-O   save
Ctrl-W   search
Ctrl-X   exit
```

Its cursor is rendered with the portable `MINI_TEXT_ATTR_INVERSE` attribute when supported; Linux maps that to reverse video below MiniShell.

## Linux tests

The current reference suite has 7 tests covering:

1. shell/application loading;
2. service semantics/lifecycle;
3. terminal Input;
4. portable utilities;
5. nano PTY edit/save/exit and inverse cursor;
6. directory iteration/root `ls` behavior;
7. resource quota plus `free`/`df`/`date` behavior.

## Documentation

Start with:

- `docs/README.md` — documentation map and source-of-truth order.
- `docs/architecture.md` — current architecture.
- `docs/design-principles.md` — review/design rules.
- `docs/consistency-check.md` — latest architecture audit and housekeeping debt.
- `docs/progress.md` — milestone log.

Older `task*.md` files document the original Tab5/ESP-IDF proof-of-concept path and are historical records, not the current roadmap.

## Current status

The generic Linux MiniShell baseline is complete and working. The architecture audit found the public/application design sound, with internal housekeeping debt mainly in the oversized Linux backend, the large Filesystem service implementation, small POSIX assumptions in shell/core, dormant pre-Linux source trees, and terminal CSI parser robustness.

Those items are tracked in `docs/consistency-check.md`. After housekeeping, new service work should again be driven top-down by MiniFT8-V3 requirements rather than generic feature accumulation.

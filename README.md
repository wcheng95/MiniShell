# MiniShell

MiniShell is a platform-adaptive application runtime focused on two maintained targets:

```text
Linux Mint
Cardputer ADV / ESP32-S3
```

Its purpose is to keep application cores independent of platform implementation details while providing a small shell, application lifecycle, resource policy, and public service API.

## Core model

```text
Applications
  FT8 / CW / future protocol apps / tools
                    |
              MiniShell API
                    |
        portable MiniShell core
  shell / lifecycle / services / policy
                    |
       private backend interface
             /             \
            v               v
         Linux             ADV
         POSIX          ESP-IDF/HW
```

Applications use MiniShell services only. Mocks and simulated providers also live below MiniShell.

## Naming rule

Project/product names remain **MiniShell** and **MiniFT8** in prose. Code-facing names use lowercase/snake_case. Runtime applications use short lowercase names:

```text
minishell
ft8
apps/ft8/
```

Normal C conventions still apply, so preprocessor macros remain uppercase.

## Shell baseline

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

Future protocol functionality is added as separate applications rather than hidden as modes inside `ft8`.

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
Linux/Mint          runtime .so + dlopen/dlsym/dlclose
Cardputer ADV V1    compiled-in registry baseline
Cardputer ADV       runtime external .elf supported
```

ADV application resolution is:

```text
1. compiled-in application
2. /flash/apps/<app>.elf
3. /sd/apps/<app>.elf
```

The same external ELF must work unchanged from `/flash/apps` or `/sd/apps`; the flash copy wins when both external copies exist.

The first planned field-usable external ADV application is Keyer:

```text
/flash/apps/keyer.elf
/sd/apps/keyer.elf
```

The user model remains `apps`, `run <app>`, direct `<app>`, application return, then `M$>`.

## Application output domains

```text
Console.write()   user-facing command/utility output
Display           interactive/full-screen application UI
System.write()    diagnostics/debugging
```

On Linux, Console and System ultimately appear in the host terminal. On Cardputer ADV, Console joins the resident text console while System remains diagnostic/debug output. Applications do not call backend-private console or display interfaces directly.

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

## Build Cardputer ADV

ESP-IDF v5.5.x is the current reference family.

```bash
cd platform/adv
idf.py build
```

See `platform/adv/README.md` for hardware-specific build, flash, storage, and validation details.

## Resource policy

Linux deliberately constrains the MiniShell application environment instead of exposing the host PC's unconstrained resources.

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

## Filesystem and configuration

Applications see one logical storage namespace:

```text
/flash
/sd
```

External ADV application binaries live under:

```text
/flash/apps/
/sd/apps/
```

Configuration ownership is:

```text
/flash/config.txt
    MiniShell-owned resident/platform configuration

/flash/<app>/setting.txt
    application-owned behavior and deployment configuration
```

Hardware-specific application settings are allowed. For example, Keyer may own dit/dah and KeyOut GPIO numbers, then request generic MiniShell Digital I/O operations. MiniShell must not interpret Keyer-domain meaning.

See `docs/architecture/configuration.md`.

## Time model

On Linux, MiniShell reads system UTC at startup and anchors it to monotonic time.

```text
MiniShell UTC = startup UTC anchor + monotonic elapsed
```

`date YYYY-MM-DD HH:MM:SS` re-anchors MiniShell UTC for the current session without changing Linux system time.

Cardputer ADV currently uses its backend time policy until RTC/GPS support is introduced through MiniShell-owned platform services.

## Public API

Source of truth:

```text
include/minishell/api.h
```

The current API generation is **v3** and exposes:

```text
App
System
Console
Memory
Filesystem
Time/Location
Display
Input
Audio
Digital I/O
```

Digital I/O V1 provides generic numeric-line `open/read/write/close` semantics with input, input-pullup, output, and open-drain output modes. Application-domain meanings and hardware assignments remain outside MiniShell.

MiniShell is still in active architectural development, so backward source and binary compatibility are not yet promised. External applications may need rebuilding for the matching MiniShell API generation. A formal stable ABI can be introduced later when distribution requirements justify it.

## Ownership model

```text
app lifecycle       app_manager
user text output    Console service
app allocations     Memory service
files/dirs/quota    Filesystem service
UTC/location        Time/Location service
logical display     Display service
logical key queue   Input service
audio streams       Audio service
digital lines       Digital I/O service
app loading         private platform runtime mechanism
```

Backends/providers supply platform primitives; portable services own application-visible semantics and lifecycle.

## MiniFT8 / `ft8`

MiniFT8-V3 is the current FT8 application. Its MiniShell runtime name is `ft8`.

Current cross-target validation uses:

```text
Linux backend + DESKTOP presentation
Linux backend + ADV presentation
ADV backend   + ADV presentation
```

MiniFT8 RX integration has reached RX-7 on Linux while preserving the platform boundary. The architecture cleanup gate C0-C4 is complete.

See `docs/MiniFT8/README.md` and `docs/project/architecture-cleanup.md`.

## Keyer direction

Keyer is the next application. K1 hardware-validated ADV runtime ELF loading, K2 added generic MiniShell Digital I/O, and K3 now provides a pure portable CW timing/state-machine engine. K4 is the first hardware integration stage.

```text
K0 architecture gate      COMPLETE
K1 ADV runtime ELF        COMPLETE
K2 Digital I/O            COMPLETE
K3 portable Keyer engine  COMPLETE
K4 GPIO KeyIn/KeyOut      NEXT
```

The K3 engine has no MiniShell or platform dependency; it consumes logical paddle/straight-key states plus monotonic time and produces logical key state/events. K4 wraps it with application-owned KeyIn/KeyOut modules through MiniShell Digital I/O.

Later stages add Audio sidetone, application-owned settings/UI, and finally the field `keyer.elf` milestone.

See `docs/keyer/README.md`.

## Testing

Linux CI/CTest covers shell/application loading, service semantics and lifecycle, filesystem/resource policy, terminal input, utilities, audio transport, Digital I/O semantics/cleanup, the portable Keyer engine, MiniFT8 UI/runtime behavior, and focused FT8 tests.

Architecture checks also enforce application/platform boundaries and MiniFT8's no-side-talk dependency rules. Cardputer ADV has a separate ESP-IDF firmware build gate.

## Documentation

Start with:

```text
docs/README.md
```

Important architecture documents:

```text
docs/architecture/architecture.md
docs/architecture/design-principles.md
docs/architecture/resident-vs-app.md
docs/architecture/configuration.md
```

Project/application plans:

```text
docs/project/architecture-cleanup.md
docs/project/progress.md
docs/MiniFT8/
docs/keyer/
```

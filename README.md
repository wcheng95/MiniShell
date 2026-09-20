# MiniShell

First QSO:
```
T [20260918 231930][7.074] CQ AG6AQ CM97 1646
R [20260918 231957][7.074] AG6AQ KO6JUF CM98 2 1644
T [20260918 232000][7.074] KO6JUF AG6AQ +02 1794
R [20260918 232027][7.074] AG6AQ KO6JUF R+14 2 1644
T [20260918 232030][7.074] KO6JUF AG6AQ RR73 2211
R [20260918 232057][7.074] AG6AQ KO6JUF 73 2 1644
```

MiniShell is a small platform-adaptive application runtime maintained for:

```text
Linux Mint
Cardputer ADV / ESP32-S3
```

The design goal is simple: application cores use a portable MiniShell API while Linux/ESP-IDF/device details stay below the platform boundary.

```text
Applications
    |
    v
MiniShell public API
    |
    v
portable MiniShell services/core
    |
    +---- Linux backend
    `---- Cardputer ADV backend
```

Applications do not call POSIX, ALSA, ESP-IDF, board APIs, FATFS, GPIO drivers, or USB stacks directly.

## Current baseline

Public MiniShell API generation: **v3**.

Current services:

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

Current major application status:

```text
MiniFT8
    live Linux/QMX RX             WORKING
    consecutive FT8 slots         WORKING
    V2 12.64 s decode cadence     COMPLETE
    AutoSeq AS-0..AS-8            COMPLETE
    daily ADIF logging            COMPLETE
    Field Day Cabrillo            COMPLETE
    physical QMX TX               COMPLETE — Linux/QMX
    live QMX band CAT sync        COMPLETE — 1 s debounce
    V -> 3 daily QSO view         COMPLETE — compact current-day log
    first two-way QSO             COMPLETE — 2026-09-18 UTC
    WinBook QMX RX/CAT/TX         PASS
    rpi3-2 AArch64 QMX RX/CAT/TX  PASS — native build

Keyer
    runtime ADV ELF               COMPLETE
    Digital I/O                   COMPLETE
    portable keyer engine         COMPLETE
    physical GPIO KeyIn/KeyOut    COMPLETE
    sidetone                      IMPLEMENTED / TRANSPORT HARDWARE-VALIDATED
```

Repo-wide current status is maintained in:

```text
docs/project/progress.md
```

## Shell

Resident shell commands are intentionally small:

```text
help
status
apps
run <app> [...]
<app> [...]
exit
```

Resident command aliases are read from `/flash/minishell/alias.txt` on each
non-built-in command lookup. For example, `f=ft8 --profile adv` makes `f` launch
that command; extra arguments follow the alias defaults. The first `=` separates
the name from the replacement, so additional `=` characters are preserved.
Built-ins take precedence, duplicate names use the last definition, and expansion
happens once. Blank/comment/invalid lines are ignored; a missing file is normal.
Edits take effect on the next command. Existing whitespace tokenization applies;
aliases do not add quoting or shell scripting.

Portable applications currently include:

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
M$> ft8
... MiniFT8 live QMX RX/TX ...
q
M$>
```

## Application model

A native application exposes:

```c
int main(int argc, char **argv);
```

and gets services through:

```c
const mini_api_t *api = mini_api_get();
```

Application packaging is platform-private:

```text
Linux              runtime .so via dlopen/dlsym/dlclose
Cardputer ADV      compiled-in registry and runtime external .elf
```

ADV resolution order:

```text
1. compiled-in application
2. /flash/apps/<app>.elf
3. /sd/apps/<app>.elf
```

## Linux build

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

## Cardputer ADV build

ESP-IDF v5.5.x is the current reference family.

```bash
cd platform/adv
idf.py build
```

See:

```text
platform/adv/README.md
```

## Resource policy

Linux deliberately constrains the MiniShell application domain rather than exposing the host's full resources.

Default application budgets:

```text
memory    8 MiB
storage  64 MiB
```

Override example:

```bash
MINISHELL_MEMORY_LIMIT=16M \
MINISHELL_STORAGE_LIMIT=128M \
./build-linux/minishell
```

`free` and `df` report these same application-visible resource domains.

## Filesystem and configuration

Applications see the logical namespace:

```text
/flash
/sd
```

General ownership rule:

```text
/flash/config.txt          MiniShell-owned platform/resident configuration
/flash/minishell/alias.txt  MiniShell resident command aliases
/flash/<app>/setting.txt   application-owned configuration
```

MiniFT8 currently retains its established configuration filename:

```text
/flash/ft8/station.txt
```

Changing that to `setting.txt` would be a separate migration.

## Time

Linux MiniShell anchors UTC to monotonic time at startup:

```text
MiniShell UTC = startup UTC + monotonic elapsed
```

The `date` application can re-anchor the MiniShell session clock without changing Linux system time.

Applications consume time only through the MiniShell Time/Location API.

## MiniFT8

MiniFT8-V3 is the current FT8 application. MiniFT8-V2 remains the behavioral reference for preserved protocol/QSO behavior.

Current live RX path:

```text
QMX USB-UAC
48 kHz / S24_3LE / stereo
    -> Linux ALSA capture worker
    -> MiniShell Audio ring
       12 kHz / S16 / stereo
    -> RxFrontend
       6 kHz mono
    -> RxSlotFramer
       960-sample blocks
    -> Ft8Engine
    -> RxResultBuilder
    -> AutoSeq / UI
```

Live decoding follows V2 timing:

```text
0.00 s    begin slot
12.64 s   decode after 79 symbols
15.00 s   advance to next slot
```

Capture continues while synchronous decoding runs so later slots do not lose audio alignment.

MiniFT8 logs through MiniShell Filesystem + Time/Location only:

```text
/flash/ft8/YYYYMMDD.txt   ADIF
/flash/ft8/fieldday.txt   ARRL Field Day Cabrillo
/flash/ft8/RTYYMMDD.txt   V2-compatible RX/TX trace
```

Linux/pc-1 + QMX has completed a real two-way FT8 QSO with physical CAT-keyed
79-symbol transmission, RX recovery, ADIF/RxTxLog persistence, CQ/POTA beacon
operation, Random/Fixed/RX offset-source support, hardware-validated live band retuning, and the validated V -> 3 current-day compact QSO view sourced from the daily ADIF log. O -> 3 updates the selected band immediately and an already-connected QMX follows the final selection after a 1-second debounce. The same pc-1-built Linux
binary/modules also run on WinBook/TW700 with QMX RX and CAT/TX after normal
Linux `dialout` permission setup.

T026 currently preserves pinned-V2 compatibility for the ambiguous string
`RR73`: if a standard decoded field is typed as GRID but its exact text is
`RR73`, MiniFT8 treats it as the terminal TX4 stage before ordinary grid
classification. This is explicitly a temporary compatibility rule because
`RR73` is also a syntactically valid Maidenhead locator; permanent
disambiguation is deferred.

Canonical MiniFT8 documentation:

```text
docs/MiniFT8/README.md
docs/MiniFT8/development.md
docs/MiniFT8/ui.md
docs/MiniFT8/architecture.md
```

Detailed `rx-*` and `as-*` files remain historical implementation/regression records.

## Keyer

Keyer is the other field-oriented MiniShell application.

Architecture:

```text
config_service
      |
      v
                  +--> keyin ------> MiniShell Digital I/O
                  +--> keyer_engine
app_controller ---+--> keyout ------> MiniShell Digital I/O
                  `--> sidetone ----> MiniShell Audio TX
```

Current real ADV GPIO baseline:

```text
G13  KeyIn tip
G15  KeyIn ring
G3   KeyOut tip
G6   KeyOut ring
```

See:

```text
docs/keyer/README.md
platform/adv/elf_apps/keyer/README.md
```

## Testing

Linux CTest covers shell/application loading, service semantics, filesystem/resource policy, terminal input, Audio, Digital I/O, Keyer, MiniFT8 UI/runtime behavior, AutoSeq, and focused FT8 DSP/protocol tests.

Architecture checks enforce the application/platform boundary and no-side-talk dependency rules.

Useful QMX Audio diagnostic:

```text
M$> audio_probe alsa:hw:2,0 5
```

## Documentation

Start here:

```text
docs/README.md
```

Core architecture:

```text
docs/architecture/architecture.md
docs/architecture/design-principles.md
docs/architecture/resident-vs-app.md
docs/architecture/configuration.md
```

Project status:

```text
docs/project/progress.md
docs/project/architecture-cleanup.md
```

Public API source of truth:

```text
include/minishell/api.h
```

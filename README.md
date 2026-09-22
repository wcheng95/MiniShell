# MiniShell

MiniShell is a small platform-adaptive application runtime for:

```text
Linux Mint
Cardputer ADV / ESP32-S3
```

The current field baseline combines three pieces:

```text
MiniShell   portable runtime + services + resident shell
MiniFT8     operational FT8 RX/TX application
Mini-CW     operational field CW/keyer application
```

The hardware-accepted field milestone is documented in:

```text
docs/project/milestone-2026-09-21-field-baseline.md
```

## Architecture

Applications use the public MiniShell API. Platform and device mechanics remain
below that boundary.

```text
Applications
    |
    v
MiniShell public API
    |
    v
portable services/runtime
    |
    +---- Linux backend
    `---- Cardputer ADV backend
```

Application code does not directly own ALSA, ESP-IDF, USB stacks, FATFS, GPIO,
board libraries, or host terminal details.

Public API generation: **v3**.

Current service families:

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
Serial/CDC
```

Canonical API and architecture documentation starts at:

```text
include/minishell/api.h
docs/README.md
```

## Operational baseline

### Cardputer ADV / MiniShell

The ADV field baseline includes:

- 20x7 resident text console with USB mirror;
- 50 physical rows of resident console scrollback;
- Fn+Up / Fn+Down scroll by five rows;
- FATFS `/flash` and optional `/sd`;
- WebFS browser file management;
- `usbmsc` storage handoff/remount;
- runtime external ELF loading;
- normalized Cardputer keyboard Input;
- generic Display colors and a provider-owned 2-pixel row separator;
- optional RTC/GPS-backed Time/Location providers.

Application resolution on ADV is:

```text
1. compiled-in application
2. /flash/apps/<app>.elf
3. /sd/apps/<app>.elf
```

### MiniFT8

MiniFT8-V3 is the production FT8 application:

```text
M$> ft8
```

Accepted behavior includes:

- continuous QMX USB-UAC RX on Linux and ADV;
- V2-compatible 12.64-second decode cadence;
- AutoSeq and QSO state;
- physical QMX CAT TX on Linux and ADV;
- daily ADIF, Field Day Cabrillo, and V2-compatible RxTxLog;
- CQ/POTA beacon operation;
- Random/Fixed/RX TX-offset selection;
- live band synchronization;
- current-day QSO view;
- RX priority ordering and retained-row lifetime;
- ADV color status:
  - separator white while idle/RX;
  - separator red during physical TX;
  - reply-to-me rows red;
  - CQ rows green;
  - other RX rows white;
- bare `;` / `.` paging on RX/TX.

Linux/pc-1 + QMX completed the first real two-way MiniFT8-V3 QSO on
2026-09-18 UTC:

```text
T [20260918 231930][7.074] CQ AG6AQ CM97 1646
R [20260918 231957][7.074] AG6AQ KO6JUF CM98 2 1644
T [20260918 232000][7.074] KO6JUF AG6AQ +02 1794
R [20260918 232027][7.074] AG6AQ KO6JUF R+14 2 1644
T [20260918 232030][7.074] KO6JUF AG6AQ RR73 2211
R [20260918 232057][7.074] AG6AQ KO6JUF 73 2 1644
```

Canonical MiniFT8 docs:

```text
docs/MiniFT8/README.md
docs/MiniFT8/development.md
docs/MiniFT8/ui.md
docs/MiniFT8/architecture.md
```

### Mini-CW

The old MiniShell `apps/keyer` implementation has been retired. Mini-CW is the
only current field CW application.

Source behavior is based on pinned standalone Mini-CW V1.2, adapted to MiniShell
services without moving CW semantics into the runtime.

Accepted Mini-CW behavior includes:

- clean paddle and automatic M1 audio;
- KeyIn/KeyOut through MiniShell Digital I/O;
- persistent `/flash/minicw/setting.txt`;
- fixed 20x7 UTC/status header;
- callsign -> operator-name lookup from
  `/flash/minicw/qsocalls.csv`;
- white/green/cyan UI with a green 2-pixel separator;
- compact daily transcript logs under `/flash/minicw/YYYYMMDD.txt`;
- dual-quote safe note mode using inline `**note**` markers.

Canonical Mini-CW docs:

```text
apps/minicw/README.md
docs/MiniCW/migration.md
docs/MiniCW/baseline-audit.md
```

On ADV the external artifact is installed as:

```text
/flash/apps/minicw.elf
or
/sd/apps/minicw.elf
```

## Resident shell

Built-in commands:

```text
help
status
apps
run <app> [...]
<app> [...]
exit
```

Aliases are loaded from:

```text
/flash/minishell/alias.txt
```

The first `=` separates alias name and replacement. Built-ins take precedence;
duplicate aliases use the last definition; expansion happens once.

Portable utility applications include:

```text
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
```

ADV also provides platform-specific utilities such as `usbmsc`.

## Configuration ownership

Current persistent ownership follows the implemented filesystem layout:

```text
/flash/minishell/setting.txt    MiniShell resident operator settings (WebFS)
/flash/minishell/alias.txt      MiniShell resident command aliases
/flash/minishell/location.txt   MiniShell persistent default location
/flash/minishell/gps_baud.txt   MiniShell resident GPS state
/flash/<app>/setting.txt        application-owned settings
```

Resident MiniShell state lives under `/flash/minishell/`, with each file owned
by the resident feature that defines its contents.

MiniFT8 retains its established configuration path:

```text
/flash/ft8/station.txt
```

Mini-CW uses:

```text
/flash/minicw/setting.txt
```

## Known ADV/QMX USB caveat

Linux normally keeps QMX enumerated in the kernel while MiniFT8 merely
closes/reopens ALSA and CDC handles.

ADV currently tears down the ESP32-S3 USB Host plus UAC/CDC class drivers when
the final QMX user exits. Restarting MiniFT8 therefore performs a fresh QMX USB
enumeration. Real QMX hardware can fail that second enumeration; power-cycling
QMX restores the first-enumeration path.

Persistent/reusable ADV QMX USB-host ownership is future architecture work, not
a blocker for the current field baseline.

## Linux build

```bash
cmake -S . -B build-linux
cmake --build build-linux -j"$(nproc)"
ctest --test-dir build-linux --output-on-failure
./build-linux/minishell
```

Runtime Linux applications are built under:

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

## Testing and project status

Linux CTest covers runtime/service contracts, filesystem/resource policy,
terminal/Input behavior, Audio, Digital I/O, MiniFT8, Mini-CW, architecture
boundaries, and focused DSP/protocol regressions.

Current repo-wide status:

```text
docs/project/progress.md
```

Field milestone:

```text
docs/project/milestone-2026-09-21-field-baseline.md
```

The project is currently in **field-use / learning mode**. New implementation
work should start from concrete field feedback or a bounded learning experiment.

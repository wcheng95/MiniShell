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

## Portable QRP radio scope

MiniShell's portable-radio scope is intentionally closed at **five modes**:

```text
CW      Mini-CW field keyer/operator station
FT8     MiniFT8 weak-signal QRP/DX
JS8     JS8Chat keyboard messaging, Normal mode only
RTTY    classic 45.45-baud / 170-Hz amateur RTTY
SSTV    Robot 36 portable image QSO
```

The design goal is not to reproduce a comprehensive desktop digital-mode suite.
Each application implements the smallest useful field subset, optimized for
simple QRP portable operation on ADV.

Protocol breadth is frozen: FT4, additional JS8 speeds, additional SSTV modes,
and a sixth portable radio mode are not planned. Feature depth inside the five
modes remains open when field use justifies it. For example, Mini-CW may grow
ADV-side QMX station controls so the Cardputer can replace more of the QMX
native UI during CW operation.

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
cd [path]
pwd
run <app> [...]
<app> [...]
exit
```

The session working directory starts at `/`. Use `cd [path]` and `pwd` to change
or display it. Startup commands and foreground apps share it; returning from an
app preserves it. Public Filesystem calls accept relative paths, for example:

```text
cd /flash/ft8
ls .
nano setting.txt
cp setting.txt /sd/backup.txt
```

`cd` without a path returns to `/`; an explicit path must name an existing
directory, and a failed change leaves CWD unchanged. Bare `ls` behaves as `ls .`;
use `ls /` for the explicit root listing.
`cp`/`mv` still require explicit destination paths. CWD is not persisted.

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

Documented configuration contract:

```text
/flash/minishell/setting.txt   MiniShell resident settings
/flash/minishell/alias.txt     MiniShell resident command aliases
/flash/<app>/setting.txt       application-owned settings
```

`/flash/minishell/setting.txt` holds WebFS SoftAP credentials and resident boot
settings, for example:

```text
SSID=MiniShell
PW=<your passphrase>
startup=ft8;b
```

`startup` runs semicolon-separated commands once before the first
prompt, using normal aliases and waiting for each foreground app to return.
Failures do not stop later commands. Empty/missing startup runs nothing.
Boot settings are bounded to a 1,024-byte file; edits take effect next boot.
See [configuration rules](docs/architecture/configuration.md) for details.

Future
operator-facing MiniShell settings, such as an exposed GPS baud setting, belong
in this same resident settings file rather than creating additional public
configuration files.

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

## Credits and references

MiniShell is an independent project, but its radio applications were developed with
help from earlier implementations, protocol references, test vectors, and engineering
tools. These projects and resources deserve explicit credit.

### FT8 / MiniFT8

- **Mini-FT8 V2** — `wcheng95/Mini-FT8` is the pinned behavioral reference for
  MiniFT8-V3. Its validated FT8 decoder/encoder behavior, generated golden WAV files,
  message handling, AutoSeq behavior, and field-operation experience provided the
  starting point for the MiniShell FT8 implementation.
- **FT8 protocol/reference implementations** used during Mini-FT8 development and
  interoperability work provided independent checks for synchronization, LDPC/CRC,
  message packing/unpacking, and on-air compatibility. MiniShell keeps its own
  portable architecture rather than depending on a desktop FT8 application at
  runtime.

### JS8

- **JS8Call-improved**, especially the pinned **v3.0.3** release, is the normative
  interoperability reference for MiniShell JS8 Normal-mode framing, Varicode,
  directed messages, Huffman/JSC data, command mappings, and wire behavior:
  <https://github.com/JS8Call-improved/JS8Call-improved>
- The original **JS8Call** source has also been useful for physical-layer and DSP
  cross-checking:
  <https://github.com/js8call/js8call>
- MiniShell's JS8 implementation is MCU-oriented and independently structured; it
  does not link the desktop JS8Call decoder.

### RTTY

- **rtty_decoder by N6HAN** provided the earlier RTTY experiments, QMX/I-Q research,
  continuous-phase AFSK test encoder, and decoder-design investigation that led to
  the MiniShell streaming RTTY receiver.
- Classic amateur **ITA2/Baudot, 45.45-baud, 170-Hz-shift RTTY** conventions and
  existing amateur-radio implementations were used to cross-check framing and
  interoperability. The MiniShell decoder itself is a small independent streaming
  implementation rather than a port of a desktop RTTY program.

### SSTV

The SSTV architecture and Robot 36 implementation were cross-checked against several
excellent public references:

- **PicoSSTV** by Dawson Jones — the primary microcontroller SSTV reference:
  <https://github.com/dawsonjon/PicoSSTV>
- **JL Barber, N7CXI**, *Proposal for SSTV Mode Specifications* (the Dayton paper).
- **unexcellent/sstv** — compact, table-driven MCU SSTV implementation:
  <https://github.com/unexcellent/sstv>
- **colaclanth/sstv** — file-oriented Martin/Scottie/Robot decoder:
  <https://github.com/colaclanth/sstv>
- **F4JTV/sstv_decoder** — streaming VIS/sync/slant-correction reference:
  <https://github.com/F4JTV/sstv_decoder>
- **JO3ALT/TinySSTV** — compact embedded SSTV reference:
  <https://github.com/JO3ALT/TinySSTV>
- Published SSTV mode/VIS tables and SSTV handbook material were also used to
  cross-check Robot 36 timing and interoperability.

MiniShell SSTV remains independently structured around its 12-kHz streaming audio
boundary, bounded-memory image pipeline, and Cardputer ADV constraints.

### CW / Mini-CW

- **Mini-CW V1.2** — `wcheng95/Mini-CW` is the hardware-behavior reference for the
  MiniShell Mini-CW application. Its keyer timing, paddle behavior, continuous-audio
  scheduling, UI concepts, and field-tested clean-audio behavior were preserved while
  platform ownership was migrated to MiniShell services.

### Engineering assistance

- **ChatGPT by OpenAI** has been used as the project supervisor/research assistant:
  architecture discussion, protocol/reference research, task decomposition,
  documentation, code/diff review, debugging analysis, and test planning.
- **OpenAI Codex** has been used as an implementation engineer for bounded repository
  tasks, including writing code and tests from architect-approved task packets.

Architecture choices, hardware experiments, on-air operation, acceptance testing, and
final project decisions remain under the project author's control. AI-generated or
AI-assisted changes are reviewed and tested before they become accepted baselines.

References above are credited for ideas, documented behavior, interoperability, and
test comparison unless a source file explicitly states otherwise. Upstream projects
retain their own copyrights and licenses; inclusion here does not change those terms.


# MiniShell Documentation Map

Canonical priority:

1. `include/minishell/api.h`
2. `docs/api/api-foundation.md`
3. service contracts under `docs/api/`
4. architecture/design-principle docs
5. runtime/application placement docs
6. application current-state docs
7. detailed stage/history docs
8. project audit/progress records

MiniShell's public contract is currently an **API**, not a frozen long-term binary ABI. Breaking API changes are allowed while the architecture is still being established. Backward source or binary compatibility will be introduced deliberately only when independently built applications make it necessary.

## Engineering workflow

Repository engineering roles and the supervisor <-> Codex handoff are defined by:

```text
AGENTS.md
```

Codex task packets and implementation notes live under:

```text
docs/project/codex/
```

The architect/tester/coordinator owns product intent, architecture decisions, hardware testing, and final merge decisions. ChatGPT acts as supervisor: it turns architecture into bounded tasks, reviews Codex diffs/PRs, diagnoses failures, and maintains current-state documentation. Codex acts as engineer and implements only the selected repository task packet.

## Public API

Current public API services:

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
Serial/CDC (optional)
```

Canonical service contracts:

```text
docs/api/api-foundation.md
docs/api/app-api.md
docs/api/system-api.md
docs/api/console-api.md
docs/api/memory-api.md
docs/api/filesystem-api.md
docs/api/time-location-api.md
docs/api/display-api.md
docs/api/input-api.md
docs/api/audio-api.md
docs/api/digital-io-api.md
docs/api/serial-api.md
```

## Architecture

Canonical architecture rules:

```text
docs/architecture/architecture.md
docs/architecture/design-principles.md
docs/architecture/resident-vs-app.md
docs/architecture/configuration.md
```

Configuration ownership:

```text
/flash/minishell/setting.txt  MiniShell resident settings
/flash/minishell/alias.txt    MiniShell resident command aliases
/flash/<app>/setting.txt      application-owned configuration/deployment settings
```

`setting.txt` is the public resident-settings location. WebFS uses it today;
future operator-facing resident settings should be added there rather than
documented as separate configuration files.

Hardware-specific application settings are allowed. Application meaning remains outside MiniShell; applications request generic services such as Audio or Digital I/O.

## Portable QRP radio scope

MiniShell deliberately limits portable radio operation to five modes:

```text
CW      Mini-CW
FT8     MiniFT8 / ft8
JS8     JS8Chat, Normal speed only
RTTY    45.45 baud / 170 Hz classic amateur profile
SSTV    Robot 36 only
```

This is a protocol-scope freeze, not a feature freeze. New work should deepen
field usefulness inside these applications rather than add more modes. FT4,
other JS8 speeds, other SSTV modes, and a sixth radio mode are outside the
planned MiniShell portable scope.

## MiniFT8

Read in this order:

```text
docs/MiniFT8/README.md       current production status and contracts
docs/MiniFT8/development.md  current engineering baseline and next work
docs/MiniFT8/ui.md           canonical UI behavior
docs/MiniFT8/architecture.md ownership/dependency architecture
```

Current MiniFT8 baseline includes continuous Linux/QMX and ADV/QMX live RX,
V2-compatible 12.64-second decoding, AutoSeq AS-0..AS-8, physical QMX FT8 TX on
Linux and ADV, a completed real two-way Linux QSO, V2-compatible RxTxLog, daily
ADIF, Field Day Cabrillo, CQ/POTA beacon control, Random/Fixed/RX TX-offset
selection, ADV color status, and bare RX/TX page shortcuts. WinBook/TW700 and
rpi3-2 also validate Linux portability.

The many `rx-*` and `as-*` documents under `docs/MiniFT8/` are retained as implementation history, design rationale, and regression anchors. They are subordinate to the four current-state documents above when old planning language conflicts with current behavior.

## Mini-CW

Current field CW application:

```text
apps/minicw/README.md
docs/MiniCW/migration.md
docs/MiniCW/baseline-audit.md
```

The Mini-CW Keyer-mode migration and follow-on features T042-T048 are hardware
accepted: clean paddle/M1 audio, settings persistence, UI/I/O cleanup, callsign
lookup, color UI, and compact transcript/note logging.

The superseded `apps/keyer` implementation and its dedicated build/tests have been removed. Historical T038/T040 task records remain under `docs/project/codex/` for engineering archaeology; Mini-CW is the only current field CW application.

## RTTY

Canonical RTTY architecture:

```text
docs/RTTY/architecture.md
```

RTTY is defined as a 12-kHz streaming external MiniShell application. The
current T065 bring-up path is Linux WAV decode; later QMX/WebSDR receive reuses
the same portable decoder core. ADV deployment remains external `rtty.elf`.

## JS8Chat and SSTV

Canonical JS8 material lives under:

```text
docs/js8/
```

JS8Chat supports the JS8 **Normal** speed only. Other JS8 speeds are not a
future roadmap item.

Canonical SSTV architecture:

```text
docs/SSTV/architecture.md
```

MiniShell SSTV is a deliberately narrow **Robot 36 RX/TX** portable/POTA image
application. Martin, Scottie, PD, and other SSTV modes are outside scope.

## Project records

Current repo-wide status:

```text
docs/project/progress.md
docs/project/milestone-2026-09-21-field-baseline.md
```

Completed architecture cleanup:

```text
docs/project/architecture-cleanup.md
```

## Runtime targets

Maintained MiniShell targets are:

```text
Linux Mint
Cardputer ADV / ESP32-S3
```

ADV application resolution remains:

```text
compiled-in
/flash/apps/<app>.elf
/sd/apps/<app>.elf
```

with earlier entries taking precedence.

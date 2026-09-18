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
/flash/config.txt          MiniShell-owned resident/platform configuration
/flash/<app>/setting.txt   application-owned configuration/deployment settings
```

Hardware-specific application settings are allowed. Application meaning remains outside MiniShell; applications request generic services such as Audio or Digital I/O.

## MiniFT8

Read in this order:

```text
docs/MiniFT8/README.md       current production status and contracts
docs/MiniFT8/development.md  current engineering baseline and next work
docs/MiniFT8/ui.md           canonical UI behavior
docs/MiniFT8/architecture.md ownership/dependency architecture
```

Current MiniFT8 baseline includes working continuous Linux/QMX live RX, V2-compatible 12.64-second decoding, AutoSeq AS-0..AS-8, daily ADIF logging, and Field Day Cabrillo logging through MiniShell APIs.

The many `rx-*` and `as-*` documents under `docs/MiniFT8/` are retained as implementation history, design rationale, and regression anchors. They are subordinate to the four current-state documents above when old planning language conflicts with current behavior.

## Keyer

Canonical entry point:

```text
docs/keyer/README.md
```

Keyer K1 runtime ELF, K2 Digital I/O, K3 portable engine, and K4 physical GPIO KeyIn/KeyOut are complete. K5 sidetone is implemented with ADV Audio TX transport hardware validation; K6 UI/settings and K7 full field validation remain future work.

## Other application/research areas

```text
docs/js8/       JS8 DSP research and future JS8Chat project
```

## Project records

Current repo-wide status:

```text
docs/project/progress.md
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

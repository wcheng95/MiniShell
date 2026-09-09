# MiniShell Progress Log

## Current baseline

- Linux Mint on `pc-1` is the reference/full production target.
- Cardputer ADV is the second real MiniShell backend.
- Runtime app discovery/loading and `M$>` are established on Linux; ADV V1 uses a compiled-in application registry.
- System, Console, Memory, Filesystem, Time/Location, Display, Input, and Audio service contracts have automated coverage; ADV Audio is intentionally not implemented yet.
- MiniFT8 is runtime app `ft8` and is FT8-only. Future FT4/CW/RTTY/JS8 support will use separate applications rather than an internal protocol-mode selector.
- MiniFT8 uses independent RX Audio, TX Audio, and Control resources.
- Audio V1 transport for MiniFT8 is 12 kHz/S16/two-channel; Linux deterministic WAV RX replays `tests/kfs16b12k.wav` unchanged.
- The application-facing contract is the **MiniShell API**. Backward source/binary compatibility is not frozen during this early architecture phase; a formal ABI may be introduced later if independently built applications require it.

## Current priority: MiniFT8 RX-1B

The P1/P2/V1 platform/profile detour is complete. RX-1A remains the frozen decoder/golden baseline and RX-1B is now active again.

Validated matrix:

```text
Linux backend + DESKTOP presentation   PASS
Linux backend + ADV presentation       PASS
ADV backend   + ADV presentation       PASS
```

Canonical V1 record:

```text
docs/MiniFT8/v1-validation.md
```

The checkpoint verified app lifecycle, shared MiniFT8 UI actions/state transitions, configuration persistence, ADV 20x7 behavior, repeated real-hardware `ft8` cycles, stable ADV memory headroom, and the absence of direct platform dependencies under `apps/ft8/`.

RX-1B now resumes the top-down decoder/module design before any RX source migration.

## Housekeeping paydown — complete

The architecture-audit debt H1-H5 is paid:

```text
H1 Linux backend split
H2 POSIX loader semantics removed from portable core
H3 Filesystem private helpers split while preserving one owner
H4 legacy Tab5 active tree removed and archived
H5 terminal ANSI/CSI + UTF-8 parser made stateful across reads
```

## A0 — portable resident shell/startup — complete

Completed normalization and control-plane cleanup:

```text
ABI-facing terminology -> API terminology
code/runtime names      -> lowercase
apps/minift8/           -> apps/ft8/
minift8 command         -> ft8
/flash/minift8/...      -> /flash/ft8/...
internal protocol Mode  -> removed from ft8
```

Portable resident shell/startup now runs through `minishell_run()` and a private backend console boundary; applications never use that private console.

Reference commit:

```text
1cb44be4  refactor: isolate resident console and startup boundary
```

## A1 — ADV ESP-IDF skeleton — complete

Validated on real Cardputer ADV:

```text
ESP-IDF / ESP32-S3 firmware
portable minishell_run()
platform : adv
USB Serial/JTAG bring-up console
compiled-in app registry
hello app lifecycle
8 MiB flash layout with 2 MiB /flash reservation
```

Reference commits:

```text
b795842e  feat: add ADV A1 ESP-IDF build skeleton
142dced2  fix: package ADV hello without CMake source mutation
```

## A2 — ADV Display/Input/Memory — complete

Real ADV providers:

```text
System          USB/debug diagnostics
Console         resident text console + USB mirror
Memory          ESP-IDF heap-backed provider
Display         20 columns x 7 rows on 240 x 135 panel
Input           TCA8418 keyboard -> MiniShell key events
```

The display backend uses `M5.Display.begin()` but not `M5.begin()`, preserving the MiniFT8-V2 audio-ownership lesson. M5/ESP-IDF details remain below MiniShell.

The portable `probe` passed Memory, Display geometry, inverse text, Input, and foreground lifecycle on real ADV.

Deferred private-console enhancement remains:

```text
resident shell history   about 50 lines
ADV viewport             7 visible rows
navigation               scroll up/down
```

## A3 — ADV Filesystem + Time/Location — complete

Storage policy:

```text
/flash    LittleFS on internal flash, initially 2 MiB (provisional)
/sd       optional FATFS on removable SD
NVS       not used
```

Validated on real ADV:

```text
SD-less boot
/flash LittleFS
optional /sd FATFS
long FATFS filenames / UTF-8
file/directory operations
a3_probe: PASS
```

ADV time policy is deliberately session-only until a real RTC/GPS source is introduced:

```text
boot UTC anchor     2026-09-01 06:00:00 UTC
monotonic advance  while powered
manual date set     current session only
power cycle         returns to fixed anchor
```

Default geographic location may persist under `/flash`; UTC itself is not persisted to flash.

## P1 — MiniFT8 presentations — complete

MiniFT8 owns two presentation policies:

```text
DESKTOP   30 x 8, contextual footer
ADV       20 x 7, six main rows, no footer
```

Linux can launch either explicitly:

```text
M$> ft8 --profile desktop
M$> ft8 --profile adv
```

Presentation is application/composition policy, not backend identity and not station configuration.

## P2 — MiniFT8 on ADV — complete

The same MiniFT8 source modules are statically composed into ADV firmware. A tiny ADV wrapper supplies the default ADV presentation; MiniFT8 itself contains no Cardputer/ESP-IDF/M5 platform selection.

Real ADV validation passed:

```text
apps discovers ft8
ft8 launches as ADV 20x7
configuration persists across launches
q returns to M$>
repeated launch/exit stable
```

P2 exposed stack ownership as a real application-lifecycle requirement. ADV foreground apps now execute on a MiniShell-managed 16 KiB application task rather than borrowing ESP-IDF's `app_main` stack.

Useful pre-RX ADV memory baseline from `free`:

```text
heap free       about 282 KiB
largest block   about 228 KiB
```

Repeated `ft8` cycles left that baseline effectively unchanged.

## V1 — cross-backend/profile checkpoint — complete

Automated coverage now includes:

```text
linux_ft8             DESKTOP + ADV launch/config/persistence
ft8_ui_smoke          presentation geometry + shared logical actions
ft8_platform_boundary no direct platform dependencies under apps/ft8/
ADV registry/build    static composition regression
```

Combined with the real ADV P2 validation, V1 passes and the platform/profile detour is closed.

## Canonical plans/policy

```text
docs/project/adv-backend-plan.md
docs/MiniFT8/v1-validation.md
docs/MiniFT8/development.md
docs/MiniFT8/rx.md
docs/api/api-foundation.md
platform/adv/README.md
```

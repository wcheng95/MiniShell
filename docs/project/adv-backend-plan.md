# ADV Backend and MiniFT8 Profile Validation Plan

## Status

**COMPLETE — historical V1 validation plan**

Stages A0, A1, A2, A3, P1, P2, and V1 are complete. This document records the decisions and evidence for that completed checkpoint; later architecture work may supersede implementation choices that were deliberately temporary during V1.

Canonical V1 evidence is recorded in:

```text
docs/MiniFT8/v1-validation.md
```

### Post-V1 direction

The V1 decision to use static ADV application composition was intentionally temporary. Current architecture cleanup C3 supersedes the old "ELF later" wording as the active direction:

```text
Cardputer ADV V1    compiled-in registry       COMPLETE baseline
Cardputer ADV next  runtime /sd/<app>.elf      ACTIVE target
first field app     /sd/keyer.elf
```

The static registry may remain during transition/testing. The external loader remains a private MiniShell/backend mechanism; portable applications still use only the public MiniShell API. A long-term cross-release binary ABI is not frozen yet.

The V1 item that mentioned future `/flash/apps`/`/sd/apps` discovery is therefore **historical, not the current path convention**. The initial active external-app convention is `/sd/<app>.elf`. Collision/precedence between static and external apps will be decided during loader implementation.

## Goal

The plan existed to exercise the architecture from both directions:

```text
MiniShell backends
    Linux
    ADV

MiniFT8 presentations
    DESKTOP
    ADV
```

Required combinations:

```text
Linux backend + DESKTOP presentation   PASS
Linux backend + ADV presentation       PASS
ADV backend   + ADV presentation       PASS
```

`ADV backend + DESKTOP presentation` is not required.

The key comparison was:

```text
Linux backend + ADV presentation
            versus
ADV backend + ADV presentation
```

The MiniFT8 core and ADV presentation are the same. Only the MiniShell backend changes.

## Locked decisions for the V1 checkpoint

The following were locked for **V1**. Items 5-6 describe the historical V1 packaging choice; see the post-V1 direction above for current ELF work.

1. **MiniShell public API is the platform boundary.** MiniFT8 never includes ESP-IDF, M5/Cardputer, board-driver, POSIX, Linux, or backend-private headers.
2. **Presentation is MiniFT8 application policy, not backend identity.** Backend and presentation are independent concepts.
3. **Protocol selection is application selection.** Runtime app `ft8` is FT8-only; future `ft4`, `cw`, `rtty`, `js8`, etc. are separate applications.
4. **Linux remains the reference/full production target.** Portable-core changes must preserve Linux behavior and tests.
5. **Cardputer ADV V1 uses static application composition.** Runtime ELF loading was deferred for V1, not rejected.
6. **Compiled-in ADV applications took priority in V1.** The then-proposed `/flash/apps` then `/sd/apps` external search was never frozen as the later loader convention.
7. **MiniShell API compatibility is not frozen yet.** Breaking API changes remain acceptable when they materially improve clarity, ownership, portability, or real application fit.
8. **ADV internal persistence uses files, not NVS.** `/flash` and optional `/sd` are MiniShell filesystem namespaces.
9. **ADV must operate without an SD card.** `/flash`, the shell, and compiled-in baseline apps remain usable without `/sd`; an external app stored on `/sd` naturally requires the card containing it.
10. **Initial `/flash` allocation is 2 MiB and provisional.** It is an implementation choice, not a public API property.
11. **ADV Audio was deliberately later in V1.** It is added when application requirements justify it.
12. **MiniFT8-V2 is hardware/behavior reference material only.** Do not refactor V2 as part of V3/MiniShell work.
13. **Resident-shell scrollback was deferred.** A future private console may retain roughly 50 lines and scroll a 7-row viewport without changing the application Display API.
14. **Foreground app execution is MiniShell-owned on ADV.** Substantial apps run on a dedicated MiniShell-managed application task rather than borrowing ESP-IDF's `app_main` stack.

## A0 — portable resident shell/startup — COMPLETE

Established:

```text
minishell_run()                    portable runtime lifecycle
private resident-console boundary  backend-owned
MiniShell API terminology          canonical
lowercase runtime/source names     canonical
ft8                                standalone FT8-only app
```

Reference commit:

```text
1cb44be4  refactor: isolate resident console and startup boundary
```

## A1 — ADV ESP-IDF skeleton — COMPLETE

Established and validated on real Cardputer ADV:

```text
ESP-IDF / ESP32-S3 firmware
portable minishell_run()
platform name adv
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

## A2 — ADV System/Console/Memory/Display/Input — COMPLETE

ADV logical Display contract:

```text
physical panel       240 x 135
logical surface      20 columns x 7 rows
text capability      only for V1
row allocation       19 px
physical gap         2 px after logical row 0
```

Pixel/font/controller details remain backend-private.

Hardware ownership remains below MiniShell:

```text
ADV display hardware     adv_display
ADV keyboard/TCA8418     adv_keyboard
shared ADV I2C bus       adv_i2c
application allocations  Memory service + adv_memory provider
```

M5 libraries are backend dependencies only. ADV uses display-only initialization and does not call `M5.begin()`, preserving microphone/speaker/I2S ownership for later Audio work.

## A3 — ADV Filesystem + Time/Location — COMPLETE

Storage policy established by this historical checkpoint used the then-current internal `/flash` implementation plus optional `/sd`; later backend storage implementation changes do not alter the public namespace.

The SD card uses the proven V2 wiring:

```text
SCK   GPIO40
MISO  GPIO39
MOSI  GPIO14
CS    GPIO12
SPI   SPI2_HOST
```

Real-device A3 validation passed:

```text
SD-less boot
/flash
optional /sd
long filenames
file/directory API
a3_probe: PASS
```

ADV UTC policy was intentionally session-only until a real RTC/GPS provider is introduced:

```text
boot UTC anchor     2026-09-01 06:00:00 UTC
monotonic advance  while powered
manual correction  current session only
power cycle         returns to fixed anchor
```

UTC is not persisted to flash. Default geographic location may persist under `/flash`.

## P1 — MiniFT8 presentations on Linux — COMPLETE

Actual V1 presentation contracts:

```text
DESKTOP   30 x 8 text frame, contextual footer
ADV       20 x 7 text frame, six main rows, no footer
```

Linux launch forms:

```text
M$> ft8
M$> ft8 --profile desktop
M$> ft8 --profile adv
```

Linux defaults to DESKTOP. Presentation is not persisted in `/flash/ft8/station.txt`; the O-screen station `Profile` remains a different concept.

## P2 — MiniFT8 on ADV — COMPLETE

ADV statically composes the same MiniFT8 source modules used by Linux. A tiny ADV wrapper supplies `ADV` as the default presentation; MiniFT8 itself contains no backend detection.

Real ADV validation passed:

```text
apps discovers ft8
ft8 launches with ADV 20x7
System Info reports Presentation: ADV
configuration persists across launches
q returns cleanly to M$>
repeated launch/exit cycles remain stable
```

P2 exposed app-stack ownership as a real lifecycle requirement. ADV foreground apps now run on a MiniShell-managed application task while the resident shell remains separate.

## V1 — cross-backend/profile checkpoint — COMPLETE

Validation compares application-visible behavior rather than physical rendering implementation:

```text
app launch/exit lifecycle                       PASS
MiniFT8 default/config state                    PASS
same logical UI actions/state transitions       PASS
station.txt persistence                         PASS
ADV 20x7 identity on Linux and real ADV         PASS
resource behavior / repeated-cycle stability    PASS
apps/ft8 platform-dependency boundary           PASS
```

Automated evidence includes:

```text
linux_ft8
ft8_ui_smoke
ft8_platform_boundary
ADV static-registry test
ADV ESP-IDF firmware build
```

The platform-boundary test rejects direct ESP-IDF, FreeRTOS, Linux/POSIX, M5/Cardputer, and private backend dependencies under `apps/ft8/`.

## Intentional V1 implementation differences

These were expected and were not V1 failures:

```text
Linux app packaging      runtime ft8.so
ADV app packaging        compiled-in static registry
Linux execution          host runtime loader/process context
ADV execution            MiniShell-managed FreeRTOS app task
Linux DESKTOP UI         30 x 8 with footer
ADV UI                    20 x 7 without footer
```

Current post-V1 work deliberately changes only the ADV packaging row by adding a runtime ELF path while preserving the application-facing boundary.

## Historical resume point

At completion of V1, MiniFT8 RX development resumed from its top-down RX plan. That work has since advanced beyond this checkpoint; this document should not be used as the current project-progress tracker.

The architectural purpose of the detour was met:

```text
the MiniShell backend boundary survives a real embedded port
and
the MiniFT8 presentation boundary survives materially different environments
```

Current project direction is tracked in `architecture-cleanup.md`, `progress.md`, and `../keyer/README.md`.

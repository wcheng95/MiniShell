# ADV Backend and MiniFT8 Profile Validation Plan

## Status

**COMPLETE**

Stages A0, A1, A2, A3, P1, P2, and V1 are complete. The cross-backend/profile detour is closed and MiniFT8 development has resumed at **RX-1B — top-down RX module/interface design**.

Canonical V1 evidence is recorded in:

```text
docs/MiniFT8/v1-validation.md
```

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

## Locked decisions

1. **MiniShell public API is the platform boundary.** MiniFT8 never includes ESP-IDF, M5/Cardputer, board-driver, POSIX, Linux, or backend-private headers.
2. **Presentation is MiniFT8 application policy, not backend identity.** Backend and presentation are independent concepts.
3. **Protocol selection is application selection.** Runtime app `ft8` is FT8-only; future `ft4`, `cw`, `rtty`, `js8`, etc. are separate applications.
4. **Linux remains the reference/full production target.** Portable-core changes must preserve Linux behavior and tests.
5. **Cardputer ADV V1 uses static application composition.** Runtime ELF loading is deferred, not rejected.
6. **Compiled-in ADV applications take priority.** Future external discovery may search `/flash/apps` and then `/sd/apps` for names not provided internally.
7. **MiniShell API compatibility is not frozen yet.** Breaking API changes remain acceptable when they materially improve clarity, ownership, portability, or real application fit.
8. **ADV internal persistence uses files, not NVS.** `/flash` is LittleFS; optional `/sd` is FATFS.
9. **ADV must operate without an SD card.** `/flash`, the shell, and compiled-in apps must remain usable without `/sd`.
10. **Initial `/flash` allocation is 2 MiB and provisional.** It is an implementation choice, not a public API property.
11. **ADV Audio is deliberately later.** Do not implement QMX/microphone audio merely to satisfy this checkpoint; add it when RX/TX requires it.
12. **MiniFT8-V2 is hardware/behavior reference material only.** Do not refactor V2 as part of V3/MiniShell work.
13. **Resident-shell scrollback is deferred.** A future private console may retain roughly 50 lines and scroll a 7-row viewport without changing the application Display API.
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

Storage policy:

```text
/flash    LittleFS on internal flash, initially 2 MiB (provisional)
/sd       FATFS on removable SD, optional
NVS       not used
```

The SD card uses the proven V2 wiring:

```text
SCK   GPIO40
MISO  GPIO39
MOSI  GPIO14
CS    GPIO12
SPI   SPI2_HOST
```

ESP-IDF FATFS is configured for heap-backed long filenames, a 255-character LFN limit, and UTF-8 API encoding.

Real-device A3 validation passed:

```text
SD-less boot
/flash LittleFS
optional /sd FATFS
long filenames
file/directory API
a3_probe: PASS
```

ADV UTC policy is intentionally session-only until a real RTC/GPS provider is introduced:

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

P2 exposed app-stack ownership as a real lifecycle requirement. ADV foreground apps now run on a MiniShell-managed 16 KiB task while the resident shell remains separate.

Useful pre-RX memory baseline from `free`:

```text
heap free       about 282 KiB
largest block   about 228 KiB
```

Repeated `ft8` cycles left the baseline effectively unchanged.

## V1 — cross-backend/profile checkpoint — COMPLETE

Validation compares application-visible behavior rather than physical rendering implementation:

```text
app launch/exit lifecycle                       PASS
MiniFT8 default/config state                    PASS
same logical UI actions/state transitions       PASS
station.txt persistence                         PASS
ADV 20x7 identity on Linux and real ADV         PASS
resource behavior / repeated-cycle stability    PASS
apps/ft8 platform-dependency boundary            PASS
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

## Intentional implementation differences

These are expected and are not V1 failures:

```text
Linux app packaging      runtime ft8.so
ADV app packaging        compiled-in static registry
Linux execution          host runtime loader/process context
ADV execution            MiniShell-managed FreeRTOS app task
Linux DESKTOP UI         30 x 8 with footer
ADV UI                    20 x 7 without footer
Linux Audio              WAV/reference provider available
ADV Audio                unavailable until RX/TX requires it
```

## Resume point

V1 passed. MiniFT8 RX development resumes at:

```text
RX-1B
    -> top-down RX goal/responsibilities
    -> module boundaries
    -> ownership
    -> data contracts
    -> lifecycle/state transitions
    -> workspace/memory ownership
    -> dependency direction
    -> errors/status
    -> unit-test boundaries
    -> only then source migration
```

The architectural purpose of the detour has been met:

```text
the MiniShell backend boundary survives a real embedded port
and
the MiniFT8 presentation boundary survives materially different environments
```

# T042 — Mini-CW Keyer-mode MiniShell foundation

Status: READY

## Objective

Create the first real Mini-CW application port under MiniShell.

Reference source and behavior:

```text
repository: wcheng95/Mini-CW
commit:     3bfbf169b7c2d49a1be3e9a4c80f945edb32033e
```

Read:

```text
docs/MiniCW/migration.md
AGENTS.md
include/minishell/api.h
```

T042 is **not** a rewrite of the existing `apps/keyer`.

The goal is to preserve Mini-CW service/state behavior while replacing direct
platform ownership.

## Scope

Implement a new application:

```text
apps/minicw/
```

and an ADV external ELF target:

```text
platform/adv/elf_apps/minicw/
```

T042 supports only Mini-CW **Keyer mode** and the minimum UI/runtime needed to
exercise it.

Audio is intentionally silent in T042. T043 owns the known-good Mini-CW audio
migration.

## Source migration rule

Use the pinned Mini-CW source as the reference.

Preserve names/structure where practical:

- `app_core`
- `keyer_service`
- `keyer_decoder`
- relevant `ui_service` / `ui_screen` behavior

Do not mechanically copy Cardputer/ESP-IDF code.

When a Mini-CW module mixes domain logic and platform access, split a private
port seam rather than changing domain semantics.

Document meaningful deviations from the pinned source in this packet.

## MiniShell service mapping

### Time

Replace:

- `esp_timer`
- FreeRTOS delay/ticks used only as time/polling

with MiniShell Time/Location:

- `monotonic_us()`
- `sleep_ms()`

Do not change key timing constants.

### KeyIn / KeyOut

Replace raw Mini-CW GPIO access with MiniShell Digital I/O.

Use the existing ADV deployment lines unless the pinned Mini-CW reference proves
a different required mapping:

```text
G13 KeyIn tip
G15 KeyIn ring
G3  KeyOut tip
G6  KeyOut ring
```

Preserve Mini-CW KeyIn/KeyOut semantics and shutdown release safety.

### Display

Adapt Mini-CW Keyer-mode frame/state to MiniShell text Display.

T042 may use the existing 20x7 MiniShell Display abstraction; do not call
M5Cardputer/M5Unified from the application.

Preserve Mini-CW Keyer-mode user-visible content/flow as closely as the 20x7 API
allows. Record any rendering difference caused solely by the abstraction.

### Input

Map MiniShell logical key events to Mini-CW UI events.

Do not poll raw Cardputer keyboard hardware.

### Files

T042 may use compiled/default configuration only.

Do not port persistence yet unless a tiny read-only compatibility shim is required
to make Keyer mode usable. T044 owns full storage migration.

### Audio

T042 must not use the current MiniShell Keyer sidetone implementation as a
substitute for Mini-CW audio.

Keyer audio requests may be routed to a silent private adapter/no-op while
preserving the domain calls/state required for T043.

Do not add a new public Audio API in T042.

## Application behavior

Provide a `minicw` entry that starts directly in Mini-CW Keyer mode.

Required T042 behavior:

- initialize Mini-CW Keyer-mode state;
- show Keyer UI;
- read paddle/straight-key through MiniShell Digital I/O;
- preserve Mini-CW keyer timing/decoder behavior;
- drive KeyOut through MiniShell Digital I/O;
- update decoded text/UI;
- process the subset of keyboard/UI controls required for Keyer mode;
- exit cleanly with Ctrl+C;
- release KeyOut and all MiniShell resources.

M1-M5 configuration may use pinned-reference defaults in T042 if persistence is
not yet ported.

Automatic message scheduling should remain present if it can be preserved without
audio. The hardware test is silent; T043 adds sound.

## Architecture constraints

External `minicw.elf` must not import or include application dependencies on:

- ESP-IDF
- FreeRTOS
- M5Cardputer / M5Unified
- driver/gpio
- driver/uart
- FATFS / dirent platform filesystem
- TinyUSB
- Mini-CW `board_cardputer_adv`

Use MiniShell APIs only.

The preferred external resident import remains:

```text
mini_api_get
```

Compiler helpers may be linked into the ELF as with Keyer.

No changes to existing `apps/keyer`, MiniFT8, or their behavior.

## Portable tests

Add host tests for the imported/adapted Mini-CW Keyer behavior.

At minimum prove:

- Mini-CW keyer decoder vectors/gesture behavior preserved;
- Iambic A/B/Bug behavior preserved where covered by the reference;
- physical preemption/cancel behavior preserved;
- KeyOut mode semantics and final release;
- UI input mapping for Keyer mode;
- decoded-history updates;
- monotonic timing comes through the injected MiniShell seam;
- no platform headers in application dependency boundary.

Where Mini-CW already has useful deterministic tests/vectors, port/reuse their
behavior rather than inventing new expected results.

## Build gates

Run:

- full Linux MiniShell CTest;
- portable/unit suites;
- architecture/dependency/platform boundary checks for `minicw`;
- real ADV firmware build;
- clean `platform/adv/elf_apps/minicw` external ELF build;
- `readelf -rW` import inspection;
- `git diff --check`.

Record exact ELF size/sections/imports.

## Hardware acceptance

After supervisor review, install `minicw.elf` on ADV and verify:

1. MiniShell launches `minicw`.
2. Mini-CW Keyer-mode screen appears.
3. Paddle/straight-key input is responsive.
4. decoded text updates.
5. physical KeyOut behavior matches the pinned Mini-CW reference.
6. automatic M1/M2 selection works silently if implemented in T042.
7. Ctrl+C exits and both KeyOut lines are released.
8. repeated launch/exit does not leak observable resources.

No audio-quality judgment is part of T042.

## Non-goals

Do not:

- port Mini-CW audio yet;
- solve the existing MiniShell Keyer pop;
- port trainer modes;
- port GPS;
- port full persistence;
- add battery/deep-sleep API;
- add USB MSC;
- modify existing Keyer or FT8;
- redesign Mini-CW behavior.

## Branch

Use:

```text
codex/T042-minicw-foundation
```

No PR and no hardware testing by Codex.

Set T042 to REVIEW, push, and return the exact SHA with implementation summary,
tests, resource evidence and deviations from the pinned Mini-CW source.

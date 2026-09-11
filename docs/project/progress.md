# MiniShell Progress Log

## Current baseline

- Linux Mint on `pc-1` is the reference/full production target.
- Cardputer ADV is the second real MiniShell backend.
- Linux runtime app discovery/loading uses `.so` modules.
- ADV has hardware-validated runtime external `.elf` loading using Espressif's ELF loader.
- ADV application resolution is: compiled-in first, then `/flash/apps/<app>.elf`, then `/sd/apps/<app>.elf`.
- K1 proved a real Xtensa `elfhello.elf` from both SD and internal flash, including MiniShell API resolution, clean return/unload, repeated execution, and flash priority when both external copies exist.
- The first planned field-usable ADV external application is `keyer.elf`, valid as either `/flash/apps/keyer.elf` or `/sd/apps/keyer.elf`.
- System, Console, Memory, Filesystem, Time/Location, Display, Input, Audio, and Digital I/O are public MiniShell service domains.
- The current public API generation is v3. K2 advanced v2 -> v3, so external apps built against v2 must be rebuilt for the matching firmware.
- K3 now provides a pure portable Keyer timing/state-machine engine with no platform or MiniShell dependency.
- MiniFT8 is runtime app `ft8` and remains FT8-only. Future Keyer/FT4/RTTY/JS8 functionality remains separate applications.
- MiniFT8 RX integration has advanced through RX-7 on Linux, including the production decoded-UI golden test.
- The pre-Keyer architecture cleanup C0-C4 is complete.
- The application-facing contract is the **MiniShell API**. Backward source/binary compatibility is not frozen during this early architecture phase.

## Current priority

K0-K3 are complete. K4, GPIO KeyIn/KeyOut integration through MiniShell Digital I/O, is next:

```text
K0 architecture gate          COMPLETE
        |
        v
K1 ADV runtime ELF proof      COMPLETE
        |
        v
K2 MiniShell Digital I/O      COMPLETE
        |
        v
K3 portable Keyer engine      COMPLETE
        |
        v
K4 GPIO KeyIn/KeyOut          NEXT
        |
        v
K5 sidetone
        |
        v
...
        |
        v
keyer.elf
    |-- /flash/apps/keyer.elf
    `-- /sd/apps/keyer.elf
```

K4 will wrap the pure engine with application edge modules:

```text
MiniShell Digital I/O
        |
        v
      keyin
        |
        v
 app_controller
        |
        v
 keyer_engine
        |
        v
 app_controller
        |
        v
      keyout
        |
        v
MiniShell Digital I/O
```

No direct `keyin -> keyer_engine -> keyout` sideways orchestration is introduced; `app_controller` owns coordination.

## Digital I/O — K2 complete

K2 established the generic hardware-line service required by Keyer without introducing Keyer semantics into MiniShell.

Public API:

```text
api->digital_io
    open(config, &handle)
    read(handle, &level)
    write(handle, level)
    close(handle)
```

V1 semantics include:

```text
numeric line ID
opaque line handle
input
input + pull-up
output
open-drain output
initial output level during open
duplicate-open rejection
automatic app-exit cleanup
```

Linux supplies a deterministic virtual provider for portable development/testing.

ADV uses an application-facing GPIO allow-list rather than a fragile deny-list:

```text
G1 G2 G3 G4 G5 G6 G13 G15
```

This prevents external apps from claiming resident LCD, keyboard/I2C, SD, audio, and other built-in lines.

Canonical contract:

```text
docs/api/digital-io-api.md
```

## Portable Keyer engine — K3 complete

K3 is implemented under:

```text
apps/keyer/src/keyer_engine/
    keyer_engine.c/.h
    keyer_decoder.c/.h
```

The engine is deliberately pure C domain logic. It contains no MiniShell API call and no dependency on GPIO, Audio, Display, Filesystem, ESP-IDF, FreeRTOS, POSIX, or board code.

Its inputs are:

```text
monotonic now_us
logical dit pressed
logical dah pressed
logical straight-key pressed
```

Its primary output is logical `key_down/key_up` state plus optional element/decoded-character events.

Implemented behavior:

```text
5-60 WPM
1-unit dit / 3-unit dah
1-unit inter-element gap
opposite-paddle memory
held-squeeze alternation
Mini-CW-compatible Iambic A/B selection
Bug automatic dit + manual dah
straight-key duration classification
3-unit character timing
7-unit word timing
Morse letters/digits/punctuation decoder
Mini-CW backspace/enter/space gestures
invalid-Morse '~' marker
```

K3 intentionally preserves Mini-CW's hardware-validated June 2026 Iambic A/B behavior rather than silently changing semantics during the port.

Regression suite:

```text
tests/keyer_engine_k3_test.c
```

Latest K3 verification:

```text
Linux normal build/CTest       PASS
AS-8                           PASS
strict unit build              PASS
keyer_engine_k3_unit           PASS
FT8 reference                  PASS
```

Actual physical GPIO behavior remains K4.

## Configuration ownership

Canonical configuration ownership remains:

```text
/flash/config.txt
    MiniShell-owned resident/platform configuration

/flash/<app>/setting.txt
    application-owned configuration/deployment settings
```

Full rule:

```text
docs/architecture/configuration.md
```

Application settings may be hardware-specific. Keyer may own GPIO-number settings; MiniShell receives only generic Digital I/O operations and must not know `dit`, `dah`, paddle, or KeyOut semantics.

Keyer starts with:

```text
/flash/keyer/setting.txt
```

## Keyer runtime ELF proof — K1 complete

Cardputer ADV hardware validated the non-colliding `elfhello.elf` runtime application.

Canonical path:

```text
/sd/apps/elfhello.elf
    -> discovered by apps
    -> Xtensa ELF load/relocation
    -> mini_api_get()
    -> MiniShell Console API
    -> return
    -> unload
    -> M$>
```

The exact same ELF runs from `/flash/apps/elfhello.elf`; with both copies present MiniShell selects flash.

```text
compiled-in > /flash/apps > /sd/apps
```

Representative executable-heap measurements:

```text
before load: exec free=300828, largest=286720
after unload: exec free=301984, largest=286720
```

No K1 loader leak was observed.

ESP-IDF 5.5.1 uses `CONFIG_ESP_SYSTEM_MEMPROT_FEATURE`. Cardputer ADV has no PSRAM, so Espressif's ELF loader needs executable internal SRAM; ADV therefore disables `MEMPROT_FEATURE`. External ADV applications are trusted code, not sandboxed applications.

## Architecture cleanup — complete

```text
C0  application dependency/no-side-talk enforcement   COMPLETE
C1  opaque AppController                              COMPLETE
C2  ft8_main lifecycle/wiring only                    COMPLETE
C3  ADV ELF + compiled-in -> /flash/apps -> /sd/apps  COMPLETE
C4  configuration ownership/naming                    COMPLETE
```

Canonical record:

```text
docs/project/architecture-cleanup.md
```

## MiniFT8 configuration transition

MiniFT8 currently still uses:

```text
/flash/ft8/station.txt
```

A later MiniFT8 migration may move this to:

```text
/flash/ft8/setting.txt
```

That rename is separate from Keyer work. MiniFT8 continues to own the current `station.txt` contents until deliberately migrated.

## MiniFT8 RX integration

The Linux RX reference path reaches the production application boundary. RX-7 has passed with the golden WAV and expected decoded UI output.

The reference workflow covers:

```text
RX-1C / RX-1D / RX-1G focused decoder references
RX-2 host decoder
RX-3 frontend
RX-4 slot framer
RX-5 pure assembly
RX-6 MiniShell Audio path
RX-7 production decoded UI
```

## V1 cross-backend/profile checkpoint — complete

```text
Linux backend + DESKTOP presentation   PASS
Linux backend + ADV presentation       PASS
ADV backend   + ADV presentation       PASS
```

Canonical historical record:

```text
docs/project/adv-backend-plan.md
docs/MiniFT8/v1-validation.md
```

## ADV current storage/runtime baseline

```text
/flash    FATFS on internal flash through wear levelling
/sd       optional FATFS on MicroSD
```

External ADV applications live under:

```text
/flash/apps
/sd/apps
```

Application code sees only MiniShell logical paths and does not know FATFS, wear levelling, SPI, or board details.

## Housekeeping paydown — complete

```text
H1 Linux backend split
H2 POSIX loader semantics removed from portable core
H3 Filesystem private helpers split while preserving one owner
H4 legacy Tab5 active tree removed
H5 terminal ANSI/CSI + UTF-8 parser made stateful across reads
```

## Canonical plans/policy

```text
docs/project/architecture-cleanup.md
docs/keyer/README.md
docs/architecture/architecture.md
docs/architecture/design-principles.md
docs/architecture/configuration.md
docs/architecture/resident-vs-app.md
docs/project/adv-backend-plan.md
docs/MiniFT8/v1-validation.md
docs/MiniFT8/development.md
docs/MiniFT8/rx.md
docs/api/api-foundation.md
docs/api/digital-io-api.md
platform/adv/README.md
```

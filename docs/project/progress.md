# MiniShell Progress Log

## Current baseline

- Maintained targets: Linux Mint and Cardputer ADV / ESP32-S3.
- Linux runtime applications use `.so`; ADV supports compiled-in applications plus runtime external `.elf` loading.
- ADV application resolution is `compiled-in > /flash/apps/<app>.elf > /sd/apps/<app>.elf`.
- K1 hardware-validated runtime ELF loading with `elfhello.elf` from both SD and internal flash.
- Public MiniShell API generation is v3 and currently exposes App, System, Console, Memory, Filesystem, Time/Location, Display, Input, Audio, and Digital I/O.
- K2 Digital I/O V1 is complete on Linux and ADV.
- K3 provides a pure portable Keyer timing/state-machine engine.
- K4 GPIO KeyIn/KeyOut integration is complete on real Cardputer ADV hardware, including clean application exit with no observable RAM leakage.
- MiniFT8 remains a separate FT8-only runtime application and has Linux RX integration through RX-7.
- Architecture cleanup C0-C4 is complete.

## Current priority

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
K4 GPIO KeyIn/KeyOut          COMPLETE
        |
        v
K5 sidetone                   NEXT
```

K4 keeps orchestration in `app_controller`:

```text
config_service
      |
      v
app_controller
   /      |       \
  v       v        v
keyin  keyer_engine keyout
  |                  |
  v                  v
MiniShell Digital I/O
```

Keyer sibling modules do not orchestrate each other. The application dependency checker now enforces the Keyer graph as well as FT8's graph.

## K4 GPIO KeyIn/KeyOut — complete

Software implementation:

```text
apps/keyer/main/keyer_main.c
apps/keyer/include/keyer_types.h
apps/keyer/src/app_controller/
apps/keyer/src/config_service/
apps/keyer/src/keyin/
apps/keyer/src/keyout/
platform/adv/elf_apps/keyer/
```

Default deployment:

```text
G13  KeyIn tip    active-low input + pull-up
G15  KeyIn ring   active-low input + pull-up
G3   KeyOut tip   active-low open-drain
G6   KeyOut ring  active-low open-drain

WPM        20
Paddle     IambicA
KeyIn      Paddle
KeyOut     SK
```

KeyIn supports:

```text
Paddle
PaddleR
SK-T
SK-R
```

KeyOut supports:

```text
Paddle
PaddleR
SK
SK-M
Off
```

KeyOut safety:

```text
open level = 1 -> released/high-Z before output drive is enabled
normal exit -> force G3/G6 to 1 before close
error path  -> release outputs
SK-M        -> ring may be low while app runs, but shutdown releases both
```

K4 reads `/flash/keyer/setting.txt` if present; otherwise it uses the defaults above. Settings editing/persistence UI remains K6.

Host regression coverage:

```text
tests/keyer_k4_io_test.c
    active-low KeyIn mapping
    Paddle/PaddleR/SK-T/SK-R
    Paddle/PaddleR/SK/SK-M/Off output semantics
    release-on-close safety

tests/keyer_k4_controller_test.c
    default config
    simulated G13 press
    1 ms orchestration loop
    K3 engine -> G3/G6 default SK output
    decoded E
    q exit
    all lines released/closed
```

The ADV external Keyer project produces:

```text
platform/adv/elf_apps/keyer/build/keyer.app.elf
```

The resident ADV ELF loader continues to export only `mini_api_get`. K4 CI requires the Keyer ELF to have exactly one external jump-slot import, `mini_api_get`; application code remains self-contained rather than expanding MiniShell's resident libc export surface.

Real Cardputer ADV hardware validation passed:

```text
physical GPIO KeyIn/KeyOut path works correctly
application exits cleanly back to MiniShell
KeyOut lines are released during shutdown
no observable RAM leakage across load/run/exit
```

## K3 portable Keyer engine — complete

Implementation:

```text
apps/keyer/src/keyer_engine/
    keyer_engine.c/.h
    keyer_decoder.c/.h
```

Pure C domain logic; no MiniShell, GPIO, Audio, Filesystem, ESP-IDF, FreeRTOS, POSIX, or board dependency.

Behavior includes 5-60 WPM, dit/dah and gap timing, paddle memory, squeeze alternation, Bug mode, straight-key duration classification, Morse decoding, Mini-CW special backspace/enter/space gestures, and the invalid-Morse `~` marker.

Mini-CW's field-validated June 2026 Iambic A/B behavior is preserved deliberately.

Regression:

```text
tests/keyer_engine_k3_test.c
```

## K2 Digital I/O — complete

Canonical contract:

```text
docs/api/digital-io-api.md
```

Public operations:

```text
open / read / write / close
```

V1 modes:

```text
input
input + pull-up
output
open-drain output
```

Semantics include generic numeric line IDs, opaque handles, initial output level during open, duplicate-open rejection, and automatic app-exit cleanup.

ADV application-facing GPIO allow-list:

```text
G1 G2 G3 G4 G5 G6 G13 G15
```

## Configuration ownership

```text
/flash/config.txt
    MiniShell-owned resident/platform configuration

/flash/<app>/setting.txt
    application-owned behavior/deployment configuration
```

Keyer therefore owns:

```text
/flash/keyer/setting.txt
```

MiniFT8 still owns `/flash/ft8/station.txt`; any later rename to `/flash/ft8/setting.txt` is a separate migration.

## K1 runtime ELF proof — complete

Hardware validated:

```text
/sd/apps/elfhello.elf
/flash/apps/elfhello.elf
```

Proved discovery, relocation, `mini_api_get()`, Console API use, return/unload, repeated execution, and flash-over-SD precedence.

Representative executable heap:

```text
before load: exec free=300828, largest=286720
after unload: exec free=301984, largest=286720
```

ESP-IDF 5.5.1 uses `CONFIG_ESP_SYSTEM_MEMPROT_FEATURE`; ADV disables it so the loader can allocate executable internal SRAM. External ADV applications are trusted code, not sandboxed code.

## Architecture cleanup — complete

```text
C0  dependency/no-side-talk enforcement
C1  opaque AppController
C2  lifecycle-only ft8_main
C3  ADV external ELF resolution
C4  configuration ownership/naming
```

Canonical record: `docs/project/architecture-cleanup.md`.

## MiniFT8 RX integration

Linux reference coverage reaches RX-7:

```text
RX-1C / RX-1D / RX-1G
RX-2 host decoder
RX-3 frontend
RX-4 slot framer
RX-5 pure assembly
RX-6 MiniShell Audio path
RX-7 production decoded UI
```

## ADV storage/runtime baseline

```text
/flash    FATFS on internal flash through wear levelling
/sd       optional FATFS on MicroSD

/flash/apps
/sd/apps
```

Application code uses MiniShell logical paths and does not know FATFS, SPI, wear levelling, or board implementation details.

## Canonical documents

```text
docs/project/architecture-cleanup.md
docs/project/progress.md
docs/keyer/README.md
docs/architecture/architecture.md
docs/architecture/design-principles.md
docs/architecture/configuration.md
docs/architecture/resident-vs-app.md
docs/api/api-foundation.md
docs/api/digital-io-api.md
platform/adv/README.md
platform/adv/elf_apps/keyer/README.md
```
